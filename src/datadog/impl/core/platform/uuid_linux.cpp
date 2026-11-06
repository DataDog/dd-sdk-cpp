// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>

#include "datadog/impl/core/platform/uuid.hpp"

namespace datadog::platform {

static constexpr size_t UUID_SIZE = 16;

// The golden-ratio increment used by SplitMix64: an odd constant, so multiplying by it
// is a bijection on 64-bit integers
static constexpr uint64_t SPLITMIX64_GAMMA = 0x9E3779B97F4A7C15ULL;

/** Result of an attempt to read random bytes from the kernel. */
enum class KernelRandomResult : uint8_t {
  Ok,
  // getrandom() isn't available: the kernel predates it (ENOSYS), or a seccomp
  // filter blocks it (EPERM)
  Unsupported,
  // The kernel's random pool isn't initialized yet, or the read failed
  Failed,
};

/**
 * Fills `out` with `size` bytes from getrandom(), without blocking. Returns
 * `Unsupported` if the syscall is unavailable, and `Failed` if the random pool isn't
 * initialized yet (EAGAIN) or any other error occurs.
 */
static KernelRandomResult read_getrandom(uint8_t* out, size_t size) {
  size_t filled = 0;
  while (filled < size) {
    const ssize_t n = getrandom(out + filled, size - filled, GRND_NONBLOCK);
    if (n > 0) {
      filled += static_cast<size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == ENOSYS || errno == EPERM)) {
      // Syscall unavailable; caller may try /dev/urandom instead
      return KernelRandomResult::Unsupported;
    }
    // Pool not initialized (EAGAIN) or unexpected error; we don't wait for it
    return KernelRandomResult::Failed;
  }
  return KernelRandomResult::Ok;
}

/**
 * Returns true if the kernel's random pool has been initialized, without blocking.
 *
 * /dev/urandom never blocks, so it returns weak bytes if read before the pool is
 * initialized. /dev/random becomes readable only once the pool is initialized, so
 * polling it with a zero timeout tells us whether /dev/urandom is safe to read. On
 * kernels older than 5.6, /dev/random is readable only while the kernel's entropy
 * estimate is high enough, so this can occasionally report false after the pool is
 * initialized; the caller then uses its non-cryptographic fallback.
 */
static bool is_random_pool_initialized() {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int fd = open("/dev/random", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    // /dev/random unavailable (e.g. in a minimal chroot); can't tell
    return false;
  }
  pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLIN;
  int result = poll(&pfd, 1, 0);
  while (result < 0 && errno == EINTR) {
    result = poll(&pfd, 1, 0);
  }
  close(fd);
  return result == 1 && (pfd.revents & POLLIN) != 0;
}

/**
 * Fills `out` with `size` bytes from /dev/urandom, for kernels where getrandom() is
 * unavailable. Returns false, leaving `out` in an unspecified state, if the random
 * pool isn't initialized yet or the device can't be read.
 */
static bool read_dev_urandom(uint8_t* out, size_t size) {
  if (!is_random_pool_initialized()) {
    // Reading now would yield weak bytes; let the caller fall back instead
    return false;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    // Device unavailable; nothing more to try
    return false;
  }
  size_t filled = 0;
  bool ok = true;
  while (filled < size) {
    const ssize_t n = read(fd, out + filled, size - filled);
    if (n > 0) {
      filled += static_cast<size_t>(n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else {
      // Read error or unexpected EOF; give up on this source
      ok = false;
      break;
    }
  }
  close(fd);
  return ok;
}

/** The SplitMix64 output function: a bijective mixing of all 64 bits of `z`. */
static uint64_t splitmix64_mix(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

/** Folds `value` into the running hash `hash`. */
static uint64_t mix_in(uint64_t hash, uint64_t value) {
  return splitmix64_mix((hash ^ value) + SPLITMIX64_GAMMA);
}

static uint64_t clock_nanoseconds(clockid_t clock_id) {
  timespec ts{};
  clock_gettime(clock_id, &ts);
  return (static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL) +
         static_cast<uint64_t>(ts.tv_nsec);
}

/**
 * Fills a 16-byte UUID buffer from a non-cryptographic source, for use when the
 * kernel can't supply random bytes. The result never repeats within a process, and
 * this function is thread-safe and has no process-wide side effects.
 *
 * The buffer is split into two 64-bit values, placed so that the version and variant
 * bits that the caller sets afterward never overwrite `unique`:
 *
 *   byte:   0  1  2  3  4  5   6  7  8  9 10 11 12 13  14 15
 *          [ unique[0..5]    ][ varied[0..7]          ][unique[6..7]]
 *                               ^ version  ^ variant
 *
 * - `unique` is a bijection of a process-wide call counter, so two calls in the same
 *   process always differ in these 8 bytes.
 * - `varied` hashes the wall-clock and monotonic time, the process ID, the thread ID,
 *   and the counter, so that processes (including on different devices) that fall
 *   back at the same moment are unlikely to collide.
 *
 * For example, the first call in a process has counter 0, so `unique` is
 * splitmix64_mix(SPLITMIX64_GAMMA) = 0xE220A8397B1DCDAF, written little-endian:
 * bytes 0-5 are AF CD 1D 7B 39 A8 and bytes 14-15 are 20 E2.
 */
static void fill_fallback(uint8_t out[UUID_SIZE]) {
  static std::atomic<uint64_t> s_counter{0};
  const uint64_t count = s_counter.fetch_add(1, std::memory_order_relaxed);

  const uint64_t unique = splitmix64_mix((count + 1) * SPLITMIX64_GAMMA);

  uint64_t varied = 0;
  varied = mix_in(varied, clock_nanoseconds(CLOCK_REALTIME));
  varied = mix_in(varied, clock_nanoseconds(CLOCK_MONOTONIC));
  varied = mix_in(varied, static_cast<uint64_t>(getpid()));
  varied = mix_in(varied, std::hash<std::thread::id>{}(std::this_thread::get_id()));
  varied = mix_in(varied, count);

  for (size_t i = 0; i < 6; i++) {
    out[i] = static_cast<uint8_t>(unique >> (8 * i));
  }
  for (size_t i = 0; i < 8; i++) {
    out[6 + i] = static_cast<uint8_t>(varied >> (8 * i));
  }
  out[14] = static_cast<uint8_t>(unique >> 48);
  out[15] = static_cast<uint8_t>(unique >> 56);
}

void UuidGenerate(uint8_t out[16]) {
  // Callers such as dd_uuid_random() don't expect errno to change
  const int saved_errno = errno;

  // Prefer the kernel's CSPRNG; if getrandom() is unavailable, try /dev/urandom; if
  // the kernel can't supply random bytes without blocking, fall back
  const KernelRandomResult result = read_getrandom(out, UUID_SIZE);
  const bool have_kernel_bytes =
      result == KernelRandomResult::Ok ||
      (result == KernelRandomResult::Unsupported && read_dev_urandom(out, UUID_SIZE));
  if (!have_kernel_bytes) {
    fill_fallback(out);
  }

  // Mark the result as a version 4 (random) UUID with the RFC 9562 variant: the high
  // nibble of byte 6 is the version (0100), and the top two bits of byte 8 are the
  // variant (10)
  out[6] = static_cast<uint8_t>((out[6] & 0x0F) | 0x40);
  out[8] = static_cast<uint8_t>((out[8] & 0x3F) | 0x80);

  errno = saved_errno;
}

}  // namespace datadog::platform
