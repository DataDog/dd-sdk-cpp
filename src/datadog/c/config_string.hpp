// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "datadog/impl/types/diagnostics.hpp"

// Expands a macro constant to a string literal, e.g. DATADOG_CSTR(63) -> "63".
// Used to embed DATADOG_MAX_* limit values in static diagnostic message strings
// without any runtime formatting.
#define DATADOG_CSTR_(x) #x
#define DATADOG_CSTR(x) DATADOG_CSTR_(x)

/**
 * Copies `src` into the fixed-size buffer `dest[0..dest_size)`, truncating if
 * the source string is too long. If truncation occurs, emits `truncation_msg`
 * at warning level via `logger`. Both `src` and `truncation_msg` may be null:
 * a null or empty `src` writes an empty string; a null `truncation_msg` skips
 * the diagnostic (intended for callers with no diagnostic handler available).
 */
static inline void assign_string_truncate(
    char* dest,
    size_t dest_size,
    const char* src,
    const datadog::impl::DiagnosticLogger& logger,
    const char* truncation_msg
) {
  if (!src || src[0] == '\0') {
    dest[0] = '\0';
    return;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  int written = std::snprintf(dest, dest_size, "%s", src);
  if (truncation_msg && written >= 0 && static_cast<size_t>(written) >= dest_size) {
    logger.Warning(truncation_msg);
  }
}

/**
 * Copies `src` into the fixed-size buffer `dest[0..dest_size)`, refusing if
 * the source string would overflow the buffer. A null or empty `src` writes an
 * empty string. If the string is too long, emits `overflow_msg` at error level
 * via `logger` and leaves `dest` unchanged; pass a null `overflow_msg` to
 * suppress the diagnostic.
 */
static inline void assign_string_strict(
    char* dest,
    size_t dest_size,
    const char* src,
    const datadog::impl::DiagnosticLogger& logger,
    const char* overflow_msg
) {
  if (!src || src[0] == '\0') {
    dest[0] = '\0';
    return;
  }
  if (!std::memchr(src, '\0', dest_size)) {
    if (overflow_msg) {
      logger.Error(overflow_msg);
    }
    return;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  std::snprintf(dest, dest_size, "%s", src);
}
