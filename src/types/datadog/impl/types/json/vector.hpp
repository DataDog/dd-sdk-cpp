// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <vector>

#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

/**
 * Returns the size of a vector<T> when JSON-encoded as an array. Accounts for
 * the opening and closing brackets, one comma between each adjacent pair of
 * elements, and the encoded size of each element.
 */
template <typename T>
size_t GetJsonSize(const std::vector<T>& value) {
  size_t size = 2;  // '[' and ']'
  if (!value.empty()) {
    size += value.size() - 1;  // commas between elements
    for (const auto& elem : value) {
      size += GetJsonSize(elem);
    }
  }
  return size;
}

/**
 * JSON-encodes a vector<T> as an array. Writes '[', then each element
 * separated by commas via WriteJson<T>(), then ']'.
 */
template <typename T>
size_t WriteJson(char* dst, size_t n, const std::vector<T>& value) {
  char* ptr = dst;
  char* const dst_end = dst + n;

  *ptr++ = '[';
  for (size_t i = 0; i < value.size(); ++i) {
    if (i > 0) {
      *ptr++ = ',';
    }
    ptr += WriteJson(ptr, dst_end - ptr, value[i]);
  }
  *ptr++ = ']';

  const size_t num_bytes_written = ptr - dst;
  DATADOG_ASSERT(num_bytes_written <= n, "buffer overflow on vector encode");
  return num_bytes_written;
}

}  // namespace datadog::impl
