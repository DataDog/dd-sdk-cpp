// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

#include "support/json_serialization.hpp"

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
