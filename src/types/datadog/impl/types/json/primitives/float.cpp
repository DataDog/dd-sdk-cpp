// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/primitives/float.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstring>

#if DATADOG_HAS_DOUBLE_WRITE_FALLBACK
#include <locale.h>

#include <cstdio>
#include <cstdlib>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#endif

#include "datadog/impl/types/assert.hpp"

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

#if DATADOG_HAS_DOUBLE_TO_CHARS
  // Use std::to_chars with std::chars_format::general (%g), which is guaranteed not to
  // exceed our worst-case size of MAX_DECIMAL_DOUBLE_LEN (i.e. 24 bytes)
  auto result = std::to_chars(dst, dst + n, value, std::chars_format::general);
  DATADOG_ASSERT(result.ec == std::errc{}, "insufficient buffer size on double encode");
  return result.ptr - dst;
#else
  return WriteDoubleWithoutToChars(dst, n, value);
#endif
}

size_t GetJsonSize(const double& value) { return double_gfmt_len(value); }

size_t WriteJson(char* dst, size_t n, const double& value) {
  return double_gfmt_write(dst, n, value);
}

#if DATADOG_HAS_DOUBLE_WRITE_FALLBACK

// std::to_chars in std::chars_format::general writes the shortest decimal that parses
// back to the same double (choosing the closest one if several have that length), in
// the style of printf's %g with its default precision of 6: it uses fixed notation if
// the decimal exponent X is in [-4, 6), and scientific notation otherwise. Unlike %g,
// it doesn't round to that precision, but writes every one of the shortest digits.
// For example:
//
//   value        shortest digits   X    output
//   100.0        1                 2    100
//   1234567.0    1234567           6    1.234567e+06
//   0.0001       1                 -4   0.0001
//   0.00001      1                 -5   1e-05
//
// We find the shortest digits by reading the double's exact decimal expansion from
// snprintf, then trying each length from 1 digit up, keeping the first candidate that
// strtod parses back to the same double. Both run in the "C" locale, so that the
// decimal separator is always '.'.

// A double's exact decimal expansion has at most 767 significant digits
static constexpr size_t MAX_EXACT_DIGITS = 767;

// 17 significant digits always suffice to identify a double
static constexpr size_t MAX_SHORTEST_DIGITS = 17;

// In fixed notation, the decimal exponent of the leading digit is in [-4, 6)
static constexpr int MIN_FIXED_EXPONENT = -4;
static constexpr int MAX_FIXED_EXPONENT = 5;

/** A positive decimal value: d[0].d[1]d[2]...d[count-1] x 10^exponent. */
template <size_t Capacity>
struct DecimalDigits {
  std::array<char, Capacity> digits;
  size_t count;
  int exponent;
};

/** Returns a locale_t for the "C" locale, created on first use. */
static locale_t c_locale() {
  static const locale_t s_locale = newlocale(LC_ALL_MASK, "C", nullptr);
  return s_locale;
}

/**
 * Writes the exact decimal expansion of the positive finite double `value` to `out`,
 * without trailing zeros. Must be called with the "C" locale in effect.
 */
static void get_exact_digits(double value, DecimalDigits<MAX_EXACT_DIGITS>& out) {
  // %.766e writes 767 significant digits, enough to be exact for any double, as
  // "d.ddd...e+XX" (e.g. 0.1 is "1.000000000000000055511151231257827...e-01")
  static constexpr int EXACT_PRECISION = MAX_EXACT_DIGITS - 1;
  std::array<char, MAX_EXACT_DIGITS + 16> buffer{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  std::snprintf(buffer.data(), buffer.size(), "%.*e", EXACT_PRECISION, value);

  char* digits = out.digits.data();
  const char* p = buffer.data();
  out.count = 0;
  digits[out.count++] = *p++;
  if (*p == '.') {
    ++p;
  }
  while (*p >= '0' && *p <= '9' && out.count < MAX_EXACT_DIGITS) {
    digits[out.count++] = *p++;
  }
  // `p` is now at the 'e' that precedes the exponent
  out.exponent = std::atoi(p + 1);  // NOLINT(cert-err34-c)

  while (out.count > 1 && digits[out.count - 1] == '0') {
    --out.count;
  }
}

/**
 * Returns true if `candidate`, parsed as a decimal, yields exactly `value`. Must be
 * called with the "C" locale in effect.
 */
static bool parses_to(
    const DecimalDigits<MAX_SHORTEST_DIGITS>& candidate, double value
) {
  // Format the candidate as "d.ddde<exponent>" for strtod
  std::array<char, MAX_SHORTEST_DIGITS + 16> buffer{};
  char* p = buffer.data();
  const char* digits = candidate.digits.data();
  *p++ = digits[0];
  if (candidate.count > 1) {
    *p++ = '.';
    std::memcpy(p, digits + 1, candidate.count - 1);
    p += candidate.count - 1;
  }
  const auto remaining = static_cast<size_t>(buffer.data() + buffer.size() - p);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  std::snprintf(p, remaining, "e%d", candidate.exponent);
  return std::strtod(buffer.data(), nullptr) == value;
}

/**
 * Sets `rounded_up` to `truncated` plus one unit in its last digit, carrying into the
 * exponent if every digit is 9 (e.g. 9.99e2 becomes 1.00e3).
 */
static void round_up_last_digit(
    const DecimalDigits<MAX_SHORTEST_DIGITS>& truncated,
    DecimalDigits<MAX_SHORTEST_DIGITS>& rounded_up
) {
  rounded_up = truncated;
  char* digits = rounded_up.digits.data();
  for (size_t i = rounded_up.count; i > 0; --i) {
    if (digits[i - 1] != '9') {
      ++digits[i - 1];
      return;
    }
    digits[i - 1] = '0';
  }
  digits[0] = '1';
  ++rounded_up.exponent;
}

/**
 * Finds the shortest decimal that parses back to the positive finite double `value`,
 * given its exact expansion `exact`, and writes it to `out` without trailing zeros. If
 * two decimals of that length both parse back to `value`, chooses the one closer to
 * the exact value, breaking a tie toward an even last digit. Must be called with the
 * "C" locale in effect.
 */
static void find_shortest_digits(
    const DecimalDigits<MAX_EXACT_DIGITS>& exact,
    double value,
    DecimalDigits<MAX_SHORTEST_DIGITS>& out
) {
  const char* exact_digits = exact.digits.data();
  for (size_t count = 1; count <= MAX_SHORTEST_DIGITS; ++count) {
    // If the exact value has no more than `count` digits, it's the answer
    if (exact.count <= count) {
      std::memcpy(out.digits.data(), exact_digits, exact.count);
      out.count = exact.count;
      out.exponent = exact.exponent;
      return;
    }

    // Otherwise, the exact value lies strictly between the two `count`-digit decimals
    // that surround it: its first `count` digits, truncated, and that plus one unit
    DecimalDigits<MAX_SHORTEST_DIGITS> truncated{};
    std::memcpy(truncated.digits.data(), exact_digits, count);
    truncated.count = count;
    truncated.exponent = exact.exponent;
    DecimalDigits<MAX_SHORTEST_DIGITS> rounded_up{};
    round_up_last_digit(truncated, rounded_up);

    // Decide which is closer from the remaining digits, compared with one half unit
    const char next_digit = exact_digits[count];
    const bool more_after_next = exact.count > count + 1;
    bool prefer_rounded_up = false;
    if (next_digit > '5' || (next_digit == '5' && more_after_next)) {
      prefer_rounded_up = true;
    } else if (next_digit == '5') {
      // Exactly halfway: prefer the candidate whose last digit is even
      prefer_rounded_up = (exact_digits[count - 1] - '0') % 2 != 0;
    }

    const DecimalDigits<MAX_SHORTEST_DIGITS>& closer =
        prefer_rounded_up ? rounded_up : truncated;
    const DecimalDigits<MAX_SHORTEST_DIGITS>& farther =
        prefer_rounded_up ? truncated : rounded_up;
    const DecimalDigits<MAX_SHORTEST_DIGITS>* found = nullptr;
    if (parses_to(closer, value)) {
      found = &closer;
    } else if (parses_to(farther, value)) {
      found = &farther;
    }
    if (found != nullptr) {
      out = *found;
      // Rounding up can leave trailing zeros (e.g. 1.00e3); drop them
      const char* out_digits = out.digits.data();
      while (out.count > 1 && out_digits[out.count - 1] == '0') {
        --out.count;
      }
      return;
    }
  }

  // Unreachable, since 17 digits always identify a double; write the 17-digit value
  DATADOG_ASSERT(false, "no decimal of up to 17 digits parses back to the double");
  std::memcpy(out.digits.data(), exact_digits, MAX_SHORTEST_DIGITS);
  out.count = MAX_SHORTEST_DIGITS;
  out.exponent = exact.exponent;
}

/**
 * Writes `shortest` to `out` in the notation that std::chars_format::general chooses
 * for it, and returns the number of bytes written (at most 23).
 */
static size_t write_general(
    const DecimalDigits<MAX_SHORTEST_DIGITS>& shortest, char* out
) {
  const char* digits = shortest.digits.data();
  const size_t count = shortest.count;
  const int exponent = shortest.exponent;
  char* p = out;

  if (exponent >= 0 && exponent <= MAX_FIXED_EXPONENT) {
    // Fixed notation with an integer part: e.g. "123", "123.45", or "100000"
    const auto integer_digits = static_cast<size_t>(exponent) + 1;
    for (size_t i = 0; i < integer_digits; ++i) {
      *p++ = i < count ? digits[i] : '0';
    }
    if (count > integer_digits) {
      *p++ = '.';
      for (size_t i = integer_digits; i < count; ++i) {
        *p++ = digits[i];
      }
    }
  } else if (exponent < 0 && exponent >= MIN_FIXED_EXPONENT) {
    // Fixed notation below 1: e.g. "0.0012" for 1.2e-3
    *p++ = '0';
    *p++ = '.';
    for (int i = 0; i < -exponent - 1; ++i) {
      *p++ = '0';
    }
    for (size_t i = 0; i < count; ++i) {
      *p++ = digits[i];
    }
  } else {
    // Scientific notation, with at least two exponent digits: e.g. "1.5e+06", "1e-300"
    *p++ = digits[0];
    if (count > 1) {
      *p++ = '.';
      for (size_t i = 1; i < count; ++i) {
        *p++ = digits[i];
      }
    }
    *p++ = 'e';
    *p++ = exponent < 0 ? '-' : '+';
    const int magnitude = exponent < 0 ? -exponent : exponent;
    if (magnitude >= 100) {
      *p++ = static_cast<char>('0' + (magnitude / 100));
    }
    *p++ = static_cast<char>('0' + ((magnitude / 10) % 10));
    *p++ = static_cast<char>('0' + (magnitude % 10));
  }
  return static_cast<size_t>(p - out);
}

size_t WriteDoubleWithoutToChars(char* dst, size_t n, double value) {
  DATADOG_ASSERT(
      std::isfinite(value), "WriteDoubleWithoutToChars requires a finite value"
  );

  // Write to a local buffer that fits the longest output (24 bytes, e.g.
  // "-1.7976931348623157e+308"), then copy it to `dst` once its length is known
  std::array<char, 32> buffer{};
  char* p = buffer.data();
  if (std::signbit(value)) {
    *p++ = '-';
    value = -value;
  }

  if (value == 0.0) {
    // Zero has no significant digits to search for
    *p++ = '0';
  } else {
    // Format and parse in the "C" locale, restoring this thread's locale afterward
    const locale_t locale = c_locale();
    DATADOG_ASSERT(locale != nullptr, "unable to create the C locale");
    const locale_t previous_locale = uselocale(locale);

    DecimalDigits<MAX_EXACT_DIGITS> exact{};
    get_exact_digits(value, exact);
    DecimalDigits<MAX_SHORTEST_DIGITS> shortest{};
    find_shortest_digits(exact, value, shortest);

    uselocale(previous_locale);
    p += write_general(shortest, p);
  }

  const auto len = static_cast<size_t>(p - buffer.data());
  DATADOG_ASSERT(len <= n, "insufficient buffer size on double encode");
  std::memcpy(dst, buffer.data(), len);
  return len;
}

#endif  // DATADOG_HAS_DOUBLE_WRITE_FALLBACK

}  // namespace datadog::impl
