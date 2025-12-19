// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

/**
 * If WITH_DATADOG_ALLOCATION_TRACKING is enabled for the build, the repl binary will be
 * compiled with custom implementations the new and delete operators which will profile
 * all calls made to those operators while `start-profile memory` is active.
 *
 * The tracker implementation uses fixed-size static buffers to record information about
 * alloc and free events. If it runs out of space during profiling, the resulting stats
 * will have a nonzero `num_events_dropped` value. If this value is set, indicating
 * incomplete results, a call made to `stop-profile` will produce an error, causing the
 * repl invocation to fail if called with `--abort-on-error`.
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

/**
 * Clears all state and begins recording of all calls to new/delete, up to a maximum of
 * MAX_ALLOCATION_TRACKING_EVENTS recorded.
 */
void StartAllocationTracking();

/**
 * Stops activation tracking and returns an integer indicating the number of events that
 * were dropped: if nonzero, available results should be considered incomplete.
 */
size_t StopAllocationTracking();

/**
 * Consumes all events that have been recorded since the last call to this function: may
 * be used while allocation tracking is active. First element is a pointer to the first
 * event that may be read; second is the total number of elements that may be read from
 * that address.
 */
std::pair<AllocEvent*, size_t> ReadAvailableAllocationEvents();

#endif  // WITH_DATADOG_ALLOCATION_TRACKING
