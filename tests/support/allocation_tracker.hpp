// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

/**
 * If WITH_DATADOG_ALLOCATION_TRACKING is enabled for the build, our test binaries will
 * be compiled with custom implementations the new and delete operators which will
 * profile all calls made to those operators during the execution of a test.
 *
 * Note that the tracker will only profile new and delete, which should be sufficient
 * for profiling our usage of STL containers and direct allocation via C++ constructs
 * like new, make_unique, etc. Allocations made via direct calls to cstdlib malloc (or
 * custom malloc implementations), direct system calls, etc. will not be recorded by our
 * tracker.
 *
 * The `AllocationTracker` type is provided for use within individual unit tests. Define
 * a tracker, enclose all code to be profiled in a nested scope, and then call the
 * tracker's `Stop()` function to get an `AllocationTracker::Stats` value. Check the
 * `enabled` flag on that value before proceeding: if WITH_DATADOG_ALLOCATION_TRACKING
 * is disabled, the tracker will do nothing and `enabled` will be false.
 *
 * The tracker implementation uses fixed-size static buffers to record information about
 * alloc and free events. If it runs out of space during profiling, the resulting stats
 * will have a nonzero `num_events_dropped` value. If this value is set, indicating
 * incomplete results, tests should fail and you should consider increasing
 * `MAX_ALLOCATION_TRACKING_EVENTS`.
 *
 * Our profiled implementations of `operator new`, `operator new[]`, `operator delete`,
 * and `operator delete[]` should generally behave as the standard implementations do.
 * If used outside the scope of an `AllocationTracker`, they should record no data and
 * have no significant overhead. However, they are not guaranteed to exactly replicate
 * the standard-library versions, and as such they should not be used in test
 * environments that aim to strictly replicate a production environment.
 *
 * This profiling code is specific to our `tests` target; it is not included in builds
 * of the library itself.
 */
#ifndef WITH_DATADOG_ALLOCATION_TRACKING
#define WITH_DATADOG_ALLOCATION_TRACKING 0
#endif

#if WITH_DATADOG_ALLOCATION_TRACKING

/**
 * Custom header that will be positioned just before the user bytes in all allocations
 * made by our allocation tracker.
 */
struct alignas(16) AllocHeader {
  void* raw;    // Pointer we must pass to free, incl. header/padding before user bytes
  size_t size;  // Number of user bytes requested, not including header/padding
  uint8_t overaligned;  // True if requested alignment exceeded max_align_t: on Windows,
                        // we must match _aligned_malloc with _aligned_free if set
  uint8_t pad[15];      // Explicit padding to make MSVC happy
};

static_assert(
    sizeof(AllocHeader) % alignof(AllocHeader) == 0, "AllocHeader misaligned"
);
static_assert(sizeof(AllocHeader) == 32, "unexpected AllocHeader size");

/**
 * Operation recorded in an append-only log of allocation-related events.
 */
enum class AllocOp : uint8_t { Alloc, Free };

/**
 * Record of a single call to new or delete.
 *
 * Note that this test allocator only profiles new/delete, which should be sufficient
 * for profiling our usage of STL containers and direct allocation via C++ constructs
 * like new, make_unique, etc.
 *
 * Allocations made via direct calls to cstdlib malloc (or custom malloc
 * implementations), direct system calls, etc. will not be recorded by our allocation
 * tracker.
 */
struct AllocEvent {
  uint64_t seq;  // Monotonic sequence number
  size_t tid;    // Hash of the thread ID from which the call originated
  AllocOp op;    // Operation that was called; alloc or free
  void* ptr;     // Address returned to user on alloc
  size_t size;   // Number of user bytes reserved from ptr onward
};

// new/delete overloads

void* operator new(size_t count);
void* operator new(size_t count, const std::nothrow_t& tag) noexcept;
void* operator new(size_t count, std::align_val_t al);

void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, size_t count) noexcept;
void operator delete(void* ptr, const std::nothrow_t& tag) noexcept;
void operator delete(void* ptr, std::align_val_t al) noexcept;
void operator delete(void* ptr, size_t count, std::align_val_t al) noexcept;

void* operator new[](size_t count);
void* operator new[](size_t count, const std::nothrow_t& tag) noexcept;
void* operator new[](size_t count, std::align_val_t al);

void operator delete[](void* ptr) noexcept;
void operator delete[](void* ptr, size_t count) noexcept;
void operator delete[](void* ptr, const std::nothrow_t& tag) noexcept;
void operator delete[](void* ptr, std::align_val_t al) noexcept;
void operator delete[](void* ptr, size_t count, std::align_val_t al) noexcept;

#endif  // WITH_DATADOG_ALLOCATION_TRACKING

/**
 * RAII wrapper used in unit tests to enable allocation tracking for the scope of the
 * test. Automatically starts allocation tracking on construction; automatically stops
 * it when leaving scope, if Stop() is not called manually.
 *
 * Call Stop() manually at the end of the test to stop tracking allocations and examine
 * the stats collected during the test. If the code under test might allocate memory
 * from multiple threads, Stop() MUST be called AFTER joining on those threads.
 */
struct AllocationTracker {
  /**
   * Summarized results of allocations and frees that occurred during the lifetime of
   * this AllocationTracker, until the point where Stop() was called.
   *
   * If `enabled` is false, allocation tracking is not enabled in this build, and no
   * checks may be performed.
   *
   * If `num_events_dropped` is nonzero, the tracker ran out of space and was unable to
   * completely capture the details of all alloc and free events. In this case, the
   * results should be discarded and tests that rely on these stats should fail.
   */
  struct Stats {
    bool enabled{false};

    size_t num_events_dropped{0};

    size_t num_allocs{0};
    size_t num_frees{0};

    size_t num_bytes_allocated{0};
    size_t num_bytes_freed{0};

    size_t min_alloc_size{0};
    size_t mean_alloc_size{0};
    size_t max_alloc_size{0};
  };

  AllocationTracker();
  ~AllocationTracker();

  AllocationTracker(const AllocationTracker&) = delete;
  AllocationTracker& operator=(const AllocationTracker&) = delete;
  AllocationTracker(AllocationTracker&&) = delete;
  AllocationTracker& operator=(AllocationTracker&&) = delete;

  Stats Stop();

 private:
#if WITH_DATADOG_ALLOCATION_TRACKING
  bool _stopped{false};
#endif
  Stats _stats;
};
