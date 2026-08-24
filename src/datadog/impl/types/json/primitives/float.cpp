// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/primitives/float.hpp"

#include <charconv>
#include <cmath>
#include <cstring>

#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

/**
 * Returns the worst-case number of bytes required to represent a double-precision
 * IEEE-754 value as a string, in 'general' (%g) format. Assumes that value is finite.
 */
static size_t double_gfmt_len(double value) {
  // Non-finite values (NaN, -inf, +inf) can not be expressed as valid JSON
  // numbers; replace them with literal null
  if (!std::isfinite(value)) {
    return 4;
  }

  // chars_format::general (%g) specifies 17 significant digits, plus 1 byte each for
  // sign, decimal point, exponent marker, exponent sign, and 3 bytes for exponent value
  // e.g. '-1.7976931348623157e+308`
  static constexpr size_t MAX_DECIMAL_DOUBLE_LEN = 24;

  // There's no way to precompute the exact size required to store a double as a string
  // without actually performing the (relatively costly) conversion: use the worst-case
  // length and accept a bit of waste in our preallocated sizes.
  return MAX_DECIMAL_DOUBLE_LEN;
}

static size_t double_gfmt_write(char* dst, size_t n, double value) {
  // If value is NaN, -inf, or +inf, write a literal null
  if (!std::isfinite(value)) {
    DATADOG_ASSERT(n >= 4, "insufficient buffer size on null double write");
    std::memcpy(dst, "null", 4);  // NOLINT(bugprone-not-null-terminated-result)
    return 4;
  }

  // Use std::to_chars with std::chars_format::general (%g), which is guaranteed not to
  // exceed our worst-case size of MAX_DECIMAL_DOUBLE_LEN (i.e. 24 bytes)
  auto result = std::to_chars(dst, dst + n, value, std::chars_format::general);
  DATADOG_ASSERT(result.ec == std::errc{}, "insufficient buffer size on double encode");
  return result.ptr - dst;
}

size_t GetJsonSize(const double& value) { return double_gfmt_len(value); }

size_t WriteJson(char* dst, size_t n, const double& value) {
  return double_gfmt_write(dst, n, value);
}

}  // namespace datadog::impl
