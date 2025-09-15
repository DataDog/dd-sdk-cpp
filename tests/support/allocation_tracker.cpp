#include "support/allocation_tracker.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <thread>

#include "assert.hpp"

#if WITH_DATADOG_ALLOCATION_TRACKING

// State used while allocation tracking is running: every allocation/free will result in
// a new element being added to s_events, up to the max, at which point
// s_num_dropped_events will be incremented
static const size_t MAX_ALLOCATION_TRACKING_EVENTS = 4096;
static AllocEvent s_events[MAX_ALLOCATION_TRACKING_EVENTS];
static std::atomic<size_t> s_num_events{0};
static std::atomic<size_t> s_num_dropped_events{0};
static std::atomic<int> s_started{0};
static std::atomic<uint64_t> s_seq{0};

static void start_allocation_tracking() {
  const int prev = s_started.load(std::memory_order_acquire);
  DATADOG_ASSERT(prev == 0, "start_allocation_tracking called twice");

  std::memset(s_events, 0, sizeof(s_events));
  s_num_events.store(0, std::memory_order_relaxed);
  s_num_dropped_events.store(0, std::memory_order_relaxed);
  s_seq.store(0, std::memory_order_relaxed);

  s_started.store(1, std::memory_order_release);
}

static size_t stop_allocation_tracking() {
  s_started.fetch_sub(1, std::memory_order_acquire);
  const size_t num_events = s_num_events.load(std::memory_order_acquire);
  return std::min(num_events, MAX_ALLOCATION_TRACKING_EVENTS);
}

static void log_alloc_event(AllocOp op, void* ptr, size_t size) noexcept {
  // Don't log any events if allocation tracking hasn't been started
  if (s_started.load(std::memory_order_relaxed) == 0) {
    return;
  }

  // Atomically claim a slot in our global buffer of events
  size_t index = s_num_events.fetch_add(1, std::memory_order_relaxed);

  // If there's no more space, drop the event
  if (index >= MAX_ALLOCATION_TRACKING_EVENTS) {
    s_num_dropped_events.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // Store the details of this operation
  static thread_local std::hash<std::thread::id> hasher;
  s_events[index] = AllocEvent{
      s_seq.fetch_add(1, std::memory_order_relaxed), hasher(std::this_thread::get_id()),
      op, ptr, size
  };
}

static size_t align_up(size_t x, size_t a) { return (x + (a - 1)) & ~(a - 1); }

static void* overaligned_alloc(size_t raw_size, size_t user_align) {
  DATADOG_ASSERT(
      user_align > alignof(std::max_align_t),
      "overaligned_alloc called with alignment <= max align"
  );
#ifdef _MSC_VER
  // MSVC: Use _aligned_malloc, which will require _aligned_free
  return _aligned_malloc(raw_size, user_align);
#else
#ifdef _ISOC11_SOURCE
  // POSIX C11: Use std::aligned_alloc, which requires size % align == 0
  const size_t rounded_size = align_up(raw_size, user_align);
  return std::aligned_alloc(user_align, rounded_size);
#else
  // POSIX C99: Use posix_memalign
  void* raw;
  if (posix_memalign(&raw, user_align, raw_size) != 0) {
    raw = nullptr;
  }
  return raw;
#endif
#endif
}

static void overaligned_free(void* raw) {
#ifdef _MSC_VER
  // MSVC: If we allocated with _aligned_malloc, we must free with _aligned_free
  _aligned_free(raw);
#else
  // POSIX: Normal free is fine to call after aligned_alloc/posix_memalign
  std::free(raw);
#endif
}

static void* backend_alloc(size_t count, size_t req_align, bool nothrow) {
  // new(0) must return non-null unique pointer; malloc(0) may be null.
  if (count == 0) {
    count = 1;
  }

  // Always align the user pointer to at least max_align_t
  size_t user_align = req_align;
  if (user_align < alignof(std::max_align_t)) {
    user_align = alignof(std::max_align_t);
  }

  // Validate we got a power-of-two alignment (required for posix_memalign, align_up)
  DATADOG_ASSERT(
      (user_align & (user_align - 1)) == 0, "alignment must be power of two"
  );

  // We place a header immediately before the user bytes: the raw block that we get from
  // malloc will encompass: <padding> + <header> + <user bytes (count)>
  const size_t header_size = sizeof(AllocHeader);
  const size_t raw_size = header_size + count + (user_align - 1);

  // Call malloc to get our raw bytes, handling per-platform differences re: alignment
  void* raw = nullptr;
  uint8_t overaligned = 0;
  if (user_align > alignof(std::max_align_t)) {
    raw = overaligned_alloc(raw_size, user_align);
    overaligned = 1;
  } else {
    raw = std::malloc(raw_size);
  }

  // If allocation failed, abort
  if (!raw) {
    if (nothrow) {
      return nullptr;
    }
    throw std::bad_alloc();
  }

  // Compute the aligned user pointer inside [raw, raw+raw_size)
  uintptr_t base = reinterpret_cast<uintptr_t>(raw);
  uintptr_t after_header = base + header_size;
  uintptr_t user_addr = align_up(after_header, user_align);
  void* user = reinterpret_cast<void*>(user_addr);

  // Place header immediately before the user bytes
  AllocHeader* header = reinterpret_cast<AllocHeader*>(user_addr - header_size);

  // Verify that the header fits within our raw allocation
  uintptr_t header_addr = reinterpret_cast<uintptr_t>(header);
  uintptr_t raw_end = base + raw_size;
  DATADOG_ASSERT(
      header_addr >= base && header_addr + header_size <= raw_end,
      "Header placement outside allocated bounds"
  );

  // Store allocation-related metadata in the header
  header->raw = raw;
  header->size = count;
  header->overaligned = overaligned;

  // Record profiling information about this allocation
  log_alloc_event(AllocOp::Alloc, user, count);

  // Return the user pointer
  return user;
}

static void backend_free(void* user) noexcept {
  // delete nullptr is a no-op
  if (!user) {
    return;
  }

  // Retrieve allocation metadata from header that precedes the user bytes
  uintptr_t header_addr = reinterpret_cast<uintptr_t>(user) - sizeof(AllocHeader);
  AllocHeader* header = reinterpret_cast<AllocHeader*>(header_addr);

  // Record profiling information about this free
  log_alloc_event(AllocOp::Free, user, header->size);

  // Call the appropriate system function to free our raw bytes
  if (header->overaligned != 0) {
    overaligned_free(header->raw);
  } else {
    std::free(header->raw);
  }
}

void* operator new(std::size_t count) {
  return backend_alloc(count, alignof(std::max_align_t), false);
}

void* operator new(std::size_t count, const std::nothrow_t&) noexcept {
  return backend_alloc(count, alignof(std::max_align_t), true);
}

void* operator new(std::size_t count, std::align_val_t al) {
  return backend_alloc(count, static_cast<size_t>(al), false);
}

void operator delete(void* ptr) noexcept { backend_free(ptr); }

void operator delete(void* ptr, std::size_t) noexcept { backend_free(ptr); }

void operator delete(void* ptr, const std::nothrow_t&) noexcept { backend_free(ptr); }

void operator delete(void* ptr, std::align_val_t) noexcept { backend_free(ptr); }

void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
  backend_free(ptr);
}

void* operator new[](std::size_t count) {
  return backend_alloc(count, alignof(std::max_align_t), false);
}

void* operator new[](std::size_t count, const std::nothrow_t&) noexcept {
  return backend_alloc(count, alignof(std::max_align_t), true);
}

void* operator new[](std::size_t count, std::align_val_t al) {
  return backend_alloc(count, static_cast<size_t>(al), false);
}

void operator delete[](void* ptr) noexcept { return backend_free(ptr); }

void operator delete[](void* ptr, std::size_t) noexcept { return backend_free(ptr); }

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
  return backend_free(ptr);
}

void operator delete[](void* ptr, std::align_val_t) noexcept {
  return backend_free(ptr);
}

void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
  return backend_free(ptr);
}

#endif  // WITH_DATADOG_ALLOCATION_TRACKING

AllocationTracker::AllocationTracker() {
#if WITH_DATADOG_ALLOCATION_TRACKING
  start_allocation_tracking();
#endif
}

AllocationTracker::~AllocationTracker() {
#if WITH_DATADOG_ALLOCATION_TRACKING
  Stop();
#endif
}

AllocationTracker::Stats AllocationTracker::Stop() {
#if WITH_DATADOG_ALLOCATION_TRACKING
  // If we've been stopped before, return the stats we computed before
  if (_stopped) {
    return _stats;
  }

  // First call: stop logging events, and get the final number of valid events
  const size_t n = stop_allocation_tracking();
  _stopped = true;

  // Record that we have valid stats
  _stats.enabled = true;

  // Record whether we dropped any events
  _stats.num_events_dropped = s_num_dropped_events.load(std::memory_order_relaxed);

  // If we recorded any allocations, set min_alloc_size to SIZE_MAX so we can properly
  // compute the min alloc size
  for (size_t i = 0; i < n; i++) {
    if (s_events[i].op == AllocOp::Alloc) {
      _stats.min_alloc_size = std::numeric_limits<size_t>::max();
      break;
    }
  }

  // Prepare to take an average of allocated sizes
  double size_sum = 0.0;
  size_t num_sizes = 0;

  // Iterate through all recorded events, accumulating final stats
  for (size_t i = 0; i < n; i++) {
    const AllocEvent& event = s_events[i];
    if (event.op == AllocOp::Alloc) {
      _stats.num_allocs++;
      _stats.num_bytes_allocated += event.size;

      _stats.min_alloc_size = std::min(_stats.min_alloc_size, event.size);
      _stats.max_alloc_size = std::max(_stats.max_alloc_size, event.size);

      size_sum += static_cast<double>(event.size);
      num_sizes++;
    } else {
      _stats.num_frees++;
      _stats.num_bytes_freed += event.size;
    }
  }

  // Compute our average allocation size, if we recorded any allocations
  if (num_sizes > 0) {
    _stats.mean_alloc_size =
        static_cast<size_t>(size_sum / static_cast<double>(num_sizes));
  }

#endif
  // Return our final stats: this value will be 0 (with !enabled) if allocation tracking
  // is not enabled in this build
  return _stats;
}
