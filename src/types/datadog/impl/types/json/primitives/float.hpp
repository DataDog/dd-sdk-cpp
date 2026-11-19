// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

// Every libc++ header, including <cstddef>, defines _LIBCPP_VERSION, checked below
#include <cstddef>

// Floating-point std::to_chars exists in every supported standard library, but Apple's
// libc++ marks it unavailable when the deployment target predates the OS versions that
// first shipped it (macOS 13.3, iOS and tvOS 16.3, watchOS 9.3), which makes any call
// to it a compile error. We check the deployment target that Clang predefines, rather
// than libc++'s _LIBCPP_AVAILABILITY_HAS_TO_CHARS_FLOATING_POINT, because libc++ only
// defines that macro from version 18 (Xcode 16), and Xcode 15 has the same restriction.
#if defined(__APPLE__) && defined(_LIBCPP_VERSION) &&             \
    ((defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) &&   \
      __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 130300) ||  \
     (defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__) &&  \
      __ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__ < 160300) || \
     (defined(__ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__) &&      \
      __ENVIRONMENT_TV_OS_VERSION_MIN_REQUIRED__ < 160300) ||     \
     (defined(__ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__) &&   \
      __ENVIRONMENT_WATCH_OS_VERSION_MIN_REQUIRED__ < 90300))
#define DATADOG_HAS_DOUBLE_TO_CHARS 0
#else
#define DATADOG_HAS_DOUBLE_TO_CHARS 1
#endif

// WriteDoubleWithoutToChars() relies on POSIX's per-thread locales, which Windows
// doesn't have. It's compiled even where std::to_chars is available, so that tests can
// compare the two.
#if defined(_WIN32)
#define DATADOG_HAS_DOUBLE_WRITE_FALLBACK 0
#else
#define DATADOG_HAS_DOUBLE_WRITE_FALLBACK 1
#endif

#if !DATADOG_HAS_DOUBLE_TO_CHARS && !DATADOG_HAS_DOUBLE_WRITE_FALLBACK
#error "Unable to write doubles: std::to_chars and the fallback are both unavailable"
#endif

namespace datadog::impl {

/**
 * Returns the worst-case number of bytes required to represent the given double value
 * as a JSON number literal.
 */
size_t GetJsonSize(const double& value);

/**
 * For 32-bit floats, widens to double and calls GetJsonSize().
 */
inline size_t GetJsonSize(const float& value) {
  const double value_f64 = value;
  return GetJsonSize(value_f64);
}

/**
 * Encodes the given double value as a JSON number literal.
 */
size_t WriteJson(char* dst, size_t n, const double& value);

/**
 * For 32-bit floats, widens to double and calls WriteJson().
 */
inline size_t WriteJson(char* dst, size_t n, const float& value) {
  const double value_f64 = value;
  return WriteJson(dst, n, value_f64);
}

#if DATADOG_HAS_DOUBLE_WRITE_FALLBACK
/**
 * Writes the finite double `value` to `dst` exactly as std::to_chars writes it in
 * std::chars_format::general, without calling std::to_chars. WriteJson() uses it where
 * floating-point std::to_chars is unavailable, such as on iOS below 16.3, and it's
 * declared here so that tests can compare it with std::to_chars.
 *
 * `n` must be at least GetJsonSize(value). Returns the number of bytes written. This
 * function is thread-safe, but it's much slower than std::to_chars.
 */
size_t WriteDoubleWithoutToChars(char* dst, size_t n, double value);
#endif

}  // namespace datadog::impl
