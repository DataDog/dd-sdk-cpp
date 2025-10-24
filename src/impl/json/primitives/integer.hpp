// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <cstddef>
#include <type_traits>

namespace datadog::impl {

/**
 * Returns the exact number of bytes needed to encode an unsigned 64-bit integer as a
 * JSON literal.
 */
size_t GetJsonSize(const uint64_t& value);

/**
 * Returns the exact number of bytes needed to encode a signed 64-bit integer as a JSON
 * literal.
 */
size_t GetJsonSize(const int64_t& value);

/**
 * Encodes an unsigned 64-bit integer as a JSON literal.
 */
size_t WriteJson(char* dst, size_t n, const uint64_t& value);

/**
 * Encodes a signed 64-bit integer as a JSON literal.
 */
size_t WriteJson(char* dst, size_t n, const int64_t& value);

/**
 * For all unsigned integer types, widens to uint64_t and calls GetJsonSize().
 */
template <typename T>
std::enable_if_t<
    std::is_integral_v<T> && !std::is_same_v<T, bool> && std::is_unsigned_v<T>,
    size_t>
GetJsonSize(const T& value) {
  const uint64_t value_u64 = value;
  return GetJsonSize(value_u64);
}

/**
 * For all signed integer types, widens to int64_t and calls GetJsonSize().
 */
template <typename T>
std::enable_if_t<
    std::is_integral_v<T> && !std::is_same_v<T, bool> && std::is_signed_v<T>,
    size_t>
GetJsonSize(const T& value) {
  const int64_t value_i64 = value;
  return GetJsonSize(value_i64);
}

/**
 * For all unsigned integer types, widens to uint64_t and calls WriteJson().
 */
template <typename T>
std::enable_if_t<
    std::is_integral_v<T> && !std::is_same_v<T, bool> && std::is_unsigned_v<T>,
    size_t>
WriteJson(char* dst, size_t n, const T& value) {
  const uint64_t value_u64 = value;
  return WriteJson(dst, n, value_u64);
}

/**
 * For all signed integer types, widens to int64_t and calls WriteJson().
 */
template <typename T>
std::enable_if_t<
    std::is_integral_v<T> && !std::is_same_v<T, bool> && std::is_signed_v<T>,
    size_t>
WriteJson(char* dst, size_t n, const T& value) {
  const int64_t value_i64 = value;
  return WriteJson(dst, n, value_i64);
}

}  // namespace datadog::impl
