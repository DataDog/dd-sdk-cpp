// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstring>
#include <optional>

#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

/**
 * Returns the size of an optional<T> when JSON-encoded. Requires 4 bytes for 'null' if
 * no value; calls GetJsonSize<T>() otherwise.
 */
template <typename T>
size_t GetJsonSize(const std::optional<T>& value) {
  if (!value.has_value()) {
    return 4;  // null
  }
  return GetJsonSize(*value);
}

/**
 * JSON-encodes an optional<T>. Writes 'null' if no value; calls WriteJson<T>()
 * otherwise.
 */
template <typename T>
size_t WriteJson(char* dst, size_t n, const std::optional<T>& value) {
  if (!value.has_value()) {
    DATADOG_ASSERT(n >= 4, "insufficient space for null optional<T> write");
    std::memcpy(dst, "null", 4);  // NOLINT(bugprone-not-null-terminated-result)
    return 4;
  }
  return WriteJson(dst, n, *value);
}

}  // namespace datadog::impl
