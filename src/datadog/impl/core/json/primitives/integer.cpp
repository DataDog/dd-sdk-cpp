// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/json/primitives/integer.hpp"

#include <charconv>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

/**
 * Returns the exact number of bytes required to represent an unsigned 64-bit integer as
 * a string in decimal format; i.e. the total number of digits in its base-10
 * representation.
 */
static size_t uint64_decimal_len(uint64_t value) {
  // LUT: powers_of_ten[n] => 10^n, for n=[0..19]
  static constexpr uint64_t powers_of_ten[20] = {
      1ull,
      10ull,
      100ull,
      1000ull,
      10000ull,
      100000ull,
      1000000ull,
      10000000ull,
      100000000ull,
      1000000000ull,
      10000000000ull,
      100000000000ull,
      1000000000000ull,
      10000000000000ull,
      100000000000000ull,
      1000000000000000ull,
      10000000000000000ull,
      100000000000000000ull,
      1000000000000000000ull,
      10000000000000000000ull
  };

  // Early-out for zero, as bit-counting intrinsics may be undefined for 0
  if (value == 0) {
    return 1;
  }

  // We want to compute `floor(log10(value))` in order to determine the number of
  // decimal digits required to represent the value in base-10: we can approximate this
  // computation in binary, with sufficiently small error that we can correct it with a
  // table lookup.

  // First, compute the number of significant bits in our value
#if defined(__GNUC__) || defined(__clang__)
  // The "count leading zeroes" (CLZ) intrinsic gives us the number of leading zeroes in
  // our int value's bit pattern, from which we can infer its bit length
  size_t num_bits = 64u - __builtin_clzll(value);
#elif defined(_MSC_VER) && defined(_M_X64)
  // The "bit scan reverse" (BSR) intrinsic gives us the 0-based index of the most
  // significant bit in our value
  unsigned long idx;
  _BitScanReverse64(&idx, value);
  size_t num_bits = static_cast<size_t>(idx + 1);
#else
  // Portable fallback: shift right until all remaining bits are zero, counting the
  // number of shifts
  size_t num_bits = 0;
  for (uint64_t x = value; x; x >>= 1) {
    ++num_bits;
  }
#endif
  // Bit count of a 64-bit value never exceeds 64
  DATADOG_ASSERT(num_bits <= 64, "computed bit width >64 for uint64");

  // Now that we have the bit length, we can approximate `floor(log10(value))` as
  // `floor(num_bits * log10(2))`. Right-shifting by 12 is an integer division by 4096,
  // and the ratio 1233/4096 approximates log10(2):
  //
  // -  log10(2) ~= 0.30102999566
  // - 1233/4096 ~= 0.30102539062
  // ----------------------------
  //   max error  < 0.000005
  size_t estimated_num_digits = (num_bits * 1233u) >> 12;

  // estimated_num_digits is guaranteed to be in the range [0..19], as num_bits will
  // never exceed 64, and 64 * (1233/4096) ~= 19.265625
  static_assert((1u * 1233u) >> 12 == 0, "lower bound must be 0");
  static_assert((64u * 1233u) >> 12 == 19, "upper bound must be 19");
  DATADOG_ASSERT(
      estimated_num_digits < 20, "computed num decimal digits >19 for uint64"
  );

  // Given max error < 0.000005, estimated_num_digits is either exactly
  // `floor(log10(value))` or it's off by one: if `value` is greater than or equal to
  // our estimated power of ten, then we've underestimated by one; otherwise, we're
  // right on the money
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  size_t correction = value >= powers_of_ten[estimated_num_digits] ? 1u : 0u;
  return estimated_num_digits + correction;
}

static size_t uint64_decimal_write(char* dst, size_t n, uint64_t value) {
  // Use std::to_chars, which for uint64 is guaranteed to use the minimal
  // representation, always succeeding so long as the buffer has sufficient space
  auto result = std::to_chars(dst, dst + n, value);
  DATADOG_ASSERT(result.ec == std::errc{}, "insufficient buffer size on uint64 encode");
  return result.ptr - dst;
}

/**
 * Returns the exact number of bytes required to represent a signed 64-bit integer as a
 * string in decimal format; i.e. the total number of base-10 digits plus an optional
 * sign byte if negative.
 */
static size_t int64_decimal_len(int64_t value) {
  // Take the two's-complement absolute value of our signed int: converting to unsigned
  // from a negative value is well-defined as a modulo 2^64. By contrast,
  // std::abs(value) would result in undefined behavior if value were INT64_MIN.
  const uint64_t magnitude =
      value < 0 ? (static_cast<uint64_t>(0) - static_cast<uint64_t>(value))
                : static_cast<uint64_t>(value);

  // Compute the required number of decimal digits, plus 1 byte for the sign if negative
  return uint64_decimal_len(magnitude) + (value < 0 ? 1u : 0u);
}

static size_t int64_decimal_write(char* dst, size_t n, int64_t value) {
  // Use std::to_chars, which for int64 is guaranteed to use the minimal representation,
  // appending a sign only if negative, and always succeeding so long as the buffer has
  // sufficient space
  auto result = std::to_chars(dst, dst + n, value);
  DATADOG_ASSERT(result.ec == std::errc{}, "insufficient buffer size on int64 encode");
  return result.ptr - dst;
}

size_t GetJsonSize(const uint64_t& value) { return uint64_decimal_len(value); }

size_t GetJsonSize(const int64_t& value) { return int64_decimal_len(value); }

size_t WriteJson(char* dst, size_t n, const uint64_t& value) {
  return uint64_decimal_write(dst, n, value);
}

size_t WriteJson(char* dst, size_t n, const int64_t& value) {
  return int64_decimal_write(dst, n, value);
}

}  // namespace datadog::impl
