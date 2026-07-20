// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdio>
#include <cstdlib>

#ifdef __GLIBC__
#include <execinfo.h>
#endif

// Asserts within the Datadog SDK implementation are disabled by default: they're
// intended primarily for internal development and therefore must be explicitly enabled
// by setting DD_ENABLE_ASSERTS=ON as a CMake build option
#ifndef WITH_DATADOG_ASSERTS
#define WITH_DATADOG_ASSERTS 0
#endif

#if WITH_DATADOG_ASSERTS

namespace datadog::impl {
inline void print_stack_trace() {
#ifdef __GLIBC__
  // This POSIX API inevitably requires C-style array-as-pointer semantics
  // NOLINTBEGIN(cppcoreguidelines-*)
  // NOLINTBEGIN(bugprone-multi-level-implicit-pointer-conversion)
  void* array[16];
  size_t size = backtrace(array, 16);
  char** strings = backtrace_symbols(array, static_cast<int>(size));

  std::fprintf(stderr, "Stack trace:\n");
  for (size_t i = 0; i < size; i++) {
    std::fprintf(stderr, "  [%zu] %s\n", i, strings[i]);
  }
  free(strings);
  // NOLINTEND(bugprone-multi-level-implicit-pointer-conversion)
  // NOLINTEND(cppcoreguidelines-*)
#endif
}
}  // namespace datadog::impl

// clang-format off

#define DATADOG_ASSERT(Cond, Msg) \
  do { /* NOLINT(cppcoreguidelines-avoid-do-while) */ \
    if (!(Cond)) { \
      std::fprintf(stderr, "Assertion failed: %s (%s:%d)\n", (Msg), __FILE__, __LINE__); /* NOLINT(cppcoreguidelines-pro-type-vararg) */ \
      datadog::impl::print_stack_trace(); \
      std::abort(); \
    } \
  } while (0)

// clang-format on

#else
#define DATADOG_ASSERT(Cond, Msg) ((void)(Cond), (void)(Msg))
#endif
