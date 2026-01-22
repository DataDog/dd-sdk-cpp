// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace datadog::impl {

/**
 * Returns the exact number of bytes required to encode the given string value as a JSON
 * string literal, including enclosing quotes and escape characters.
 */
size_t GetJsonSize(const std::string_view& value);

inline size_t GetJsonSize(const std::string& value) {
  std::string_view value_sv{value};
  return GetJsonSize(value_sv);
}

inline size_t GetJsonSize(const char* value) {
  std::string_view value_sv{value};
  return GetJsonSize(value_sv);
}

/**
 * Encodes the given string value as a quoted, escaped JSON string literal.
 */
size_t WriteJson(char* dst, size_t n, const std::string_view& value);

inline size_t WriteJson(char* dst, size_t n, const std::string& value) {
  std::string_view value_sv{value};
  return WriteJson(dst, n, value_sv);
}

inline size_t WriteJson(char* dst, size_t n, const char* value) {
  std::string_view value_sv{value};
  return WriteJson(dst, n, value_sv);
}

}  // namespace datadog::impl
