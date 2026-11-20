// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/primitives/float.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cfenv>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "support/json_serialization.hpp"

#if DATADOG_HAS_DOUBLE_WRITE_FALLBACK
#include <locale.h>
#ifdef __APPLE__
#include <xlocale.h>
#endif
#endif

using namespace datadog::impl;

TEST_CASE("float JSON serialization", "[unit][json]") {
  SECTION("M render finite doubles as JSON numbers") {
    // Ordinary values
    RequireJsonLiteral(0.0, "0");
    RequireJsonLiteral(3.141592653589793, "3.141592653589793");
    RequireJsonLiteral(0.123456000, "0.123456");
    RequireJsonLiteral(12345.60000, "12345.6");
    RequireJsonLiteral(-0.123456000, "-0.123456");
    RequireJsonLiteral(-12345.60000, "-12345.6");

    // Large integer
    RequireJsonLiteral(9007199254740992.0, "9.007199254740992e+15");

    // Subnormal
    RequireJsonLiteral(4.9406564584124654e-324, "5e-324");

    // Negative zero is allowed
    RequireJsonLiteral(-0.0, "-0");

    // Limits are OK
    RequireJsonLiteral(std::numeric_limits<double>::min(), "2.2250738585072014e-308");
    RequireJsonLiteral(std::numeric_limits<double>::max(), "1.7976931348623157e+308");
    RequireJsonLiteral(-std::numeric_limits<double>::min(), "-2.2250738585072014e-308");
    RequireJsonLiteral(-std::numeric_limits<double>::max(), "-1.7976931348623157e+308");
  }

  SECTION("M render finite floats as JSON numbers") {
    // Ordinary values
    RequireJsonLiteral(0.0f, "0");
    RequireJsonLiteral(3.141592653589793f, "3.1415927410125732");
    RequireJsonLiteral(0.123456000f, "0.12345600128173828");
    RequireJsonLiteral(12345.60000f, "12345.599609375");
    RequireJsonLiteral(-0.123456000f, "-0.12345600128173828");
    RequireJsonLiteral(-12345.60000f, "-12345.599609375");

    // Large integer
    RequireJsonLiteral(9007199254740992.0f, "9.007199254740992e+15");

    // Subnormal
    RequireJsonLiteral(1.40129846e-45f, "1.401298464324817e-45");

    // Negative zero is allowed
    RequireJsonLiteral(-0.0f, "-0");

    // Limits are OK
    RequireJsonLiteral(std::numeric_limits<float>::min(), "1.1754943508222875e-38");
    RequireJsonLiteral(std::numeric_limits<float>::max(), "3.4028234663852886e+38");
    RequireJsonLiteral(-std::numeric_limits<float>::min(), "-1.1754943508222875e-38");
    RequireJsonLiteral(-std::numeric_limits<float>::max(), "-3.4028234663852886e+38");
  }

  SECTION("M render non-finite values as JSON null") {
    // Double NaN/-inf/+inf
    RequireJsonLiteral(std::nan(""), "null");
    RequireJsonLiteral(-std::numeric_limits<double>::infinity(), "null");
    RequireJsonLiteral(std::numeric_limits<double>::infinity(), "null");

    // Float NaN/-inf/+inf
    RequireJsonLiteral(std::nanf(""), "null");
    RequireJsonLiteral(-std::numeric_limits<float>::infinity(), "null");
    RequireJsonLiteral(std::numeric_limits<float>::infinity(), "null");
  }
}

// The fallback's tests compare it with std::to_chars, so they need both
#if DATADOG_HAS_DOUBLE_WRITE_FALLBACK && DATADOG_HAS_DOUBLE_TO_CHARS

static std::string write_with_to_chars(double value) {
  char buffer[32];
  auto result =
      std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
  REQUIRE(result.ec == std::errc{});
  return {buffer, static_cast<size_t>(result.ptr - buffer)};
}

static std::string write_without_to_chars(double value) {
  char buffer[32];
  const size_t len = WriteDoubleWithoutToChars(buffer, GetJsonSize(value), value);
  return {buffer, len};
}

/** Returns the number of finite values in `values` whose outputs differ. */
template <typename Values>
static size_t count_mismatches(const Values& values) {
  size_t mismatches = 0;
  for (const double value : values) {
    if (!std::isfinite(value)) {
      continue;
    }
    const std::string want = write_with_to_chars(value);
    const std::string got = write_without_to_chars(value);
    if (got != want) {
      // Report only the first few mismatches, to keep the output readable
      if (mismatches < 10) {
        UNSCOPED_INFO("to_chars wrote " << want << ", fallback wrote " << got);
      }
      ++mismatches;
    }
  }
  return mismatches;
}

TEST_CASE("WriteDoubleWithoutToChars", "[unit][json]") {
  SECTION("M match std::to_chars for hand-picked values") {
    const double positive_values[] = {
        // Fixed notation, for decimal exponents in [-4, 6)
        0.0,
        -0.0,
        1.0,
        -2.5,
        0.1,
        0.3,
        100.0,
        123456.0,
        999999.0,
        999999.5,
        123456.789,
        3.141592653589793,
        2.0 / 3.0,
        0.0001,
        0.000123456789,
        0.00099999999999999999,
        // Scientific notation, for exponents outside that range
        1e6,
        1234567.0,
        9999995.0,
        1e20,
        1e21,
        1e22,
        1e23,
        9007199254740992.0,
        0.00001,
        0.000099999999999999999,
        1e-100,
        1.5e300,
        // Limits and subnormals
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::denorm_min(),
        4.9406564584124654e-324 * 3,
        2.2250738585072009e-308,
        // Doubles widened from floats, which have many significant digits
        static_cast<double>(3.1415927f),
        static_cast<double>(0.1f)
    };
    std::vector<double> values;
    for (const double value : positive_values) {
      values.push_back(value);
      values.push_back(-value);
    }
    CHECK(count_mismatches(values) == 0);

    // The outputs that a %g-based approach gets wrong
    CHECK(write_without_to_chars(100.0) == "100");
    CHECK(write_without_to_chars(1234567.0) == "1.234567e+06");
  }

  SECTION("M match std::to_chars for every power of 10 and 2, and their neighbors") {
    std::vector<double> values;
    for (int exponent = -325; exponent <= 309; ++exponent) {
      const double power = std::pow(10.0, exponent);
      values.push_back(power);
      values.push_back(std::nextafter(power, 0.0));
      values.push_back(std::nextafter(power, std::numeric_limits<double>::infinity()));
    }
    for (int exponent = -1074; exponent <= 1023; ++exponent) {
      const double power = std::ldexp(1.0, exponent);
      values.push_back(power);
      values.push_back(std::nextafter(power, 0.0));
      values.push_back(std::nextafter(power, std::numeric_limits<double>::infinity()));
    }
    CHECK(count_mismatches(values) == 0);
  }

  SECTION("M match std::to_chars for random values") {
    // Use a fixed seed, so that any failure can be reproduced
    std::mt19937_64 rng(20303);
    std::vector<double> values;
    for (int i = 0; i < 20000; ++i) {
      // Any bit pattern, covering every exponent and sign
      const uint64_t bits = rng();
      double value = 0.0;
      std::memcpy(&value, &bits, sizeof(value));
      values.push_back(value);

      // Short decimals, like those in typical attribute values
      const auto mantissa = static_cast<double>(rng() % 100000000);
      const auto scale = static_cast<double>(rng() % 12);
      values.push_back(mantissa / std::pow(10.0, scale));
    }
    CHECK(count_mismatches(values) == 0);
  }

  SECTION("M write '.' regardless of this thread's locale") {
    // A locale whose decimal separator is ',' may not be installed; skip if it isn't
    locale_t comma_locale = newlocale(LC_NUMERIC_MASK, "de_DE.UTF-8", nullptr);
    if (comma_locale == nullptr) {
      SKIP("de_DE.UTF-8 locale is unavailable");
    }
    const locale_t previous_locale = uselocale(comma_locale);
    const std::string got = write_without_to_chars(1.5);
    const std::string got_scientific = write_without_to_chars(1.5e-10);
    const bool locale_restored = uselocale(nullptr) == comma_locale;
    uselocale(previous_locale);
    freelocale(comma_locale);

    CHECK(got == "1.5");
    CHECK(got_scientific == "1.5e-10");
    CHECK(locale_restored);
  }

  SECTION("M write the same output regardless of this thread's rounding mode") {
    // Compute the expected output in the default round-to-nearest mode
    std::mt19937_64 rng(20303);
    std::vector<double> values = {0.1, 100.0, 1234567.0, 5.434935501336305};
    for (int i = 0; i < 2000; ++i) {
      const uint64_t bits = rng();
      double value = 0.0;
      std::memcpy(&value, &bits, sizeof(value));
      if (std::isfinite(value)) {
        values.push_back(value);
      }
    }
    std::vector<std::string> expected;
    expected.reserve(values.size());
    for (const double value : values) {
      expected.push_back(write_with_to_chars(value));
    }

    const int previous_rounding = std::fegetround();
    for (const int mode : {FE_TOWARDZERO, FE_DOWNWARD, FE_UPWARD}) {
      // Record results before restoring the rounding mode, so that a failed check
      // can't leave it changed for later tests
      REQUIRE(std::fesetround(mode) == 0);
      size_t mismatches = 0;
      std::string first_mismatch;
      for (size_t i = 0; i < values.size(); ++i) {
        const std::string got = write_without_to_chars(values[i]);
        if (got != expected[i]) {
          if (mismatches == 0) {
            first_mismatch = "expected " + expected[i] + ", got " + got;
          }
          ++mismatches;
        }
      }
      const bool rounding_restored = std::fegetround() == mode;
      std::fesetround(previous_rounding);

      INFO("rounding mode " << mode << ": " << first_mismatch);
      CHECK(mismatches == 0);
      CHECK(rounding_restored);
    }
  }
}

#endif
