// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>

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

}  // namespace datadog::impl
