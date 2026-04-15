// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <limits>

#include "support/json_serialization.hpp"

using namespace datadog::impl;

TEST_CASE("integer JSON serialization", "[unit][json]") {
  SECTION("M render signed and unsigned 64-bit ints as JSON numbers") {
    // Ordinary integer values
    RequireJsonLiteral(0ll, "0");
    RequireJsonLiteral(0ull, "0");
    RequireJsonLiteral(-8675309ll, "-8675309");
    RequireJsonLiteral(8675309ll, "8675309");
    RequireJsonLiteral(8675309ull, "8675309");
    RequireJsonLiteral(2147483647ll, "2147483647");
    RequireJsonLiteral(2147483647ull, "2147483647");
    RequireJsonLiteral(9223372036854775807ull, "9223372036854775807");

    // Limits
    RequireJsonLiteral(std::numeric_limits<int64_t>::min(), "-9223372036854775808");
    RequireJsonLiteral(std::numeric_limits<int64_t>::max(), "9223372036854775807");
    RequireJsonLiteral(std::numeric_limits<uint64_t>::max(), "18446744073709551615");

    // Power-of-ten boundaries
    RequireJsonLiteral(-100000001ll, "-100000001");
    RequireJsonLiteral(-100000000ll, "-100000000");
    RequireJsonLiteral(-99999999ll, "-99999999");
    RequireJsonLiteral(9999999999999ll, "9999999999999");
    RequireJsonLiteral(10000000000000ll, "10000000000000");
    RequireJsonLiteral(10000000000001ll, "10000000000001");
    RequireJsonLiteral(9999999999999999999ull, "9999999999999999999");
    RequireJsonLiteral(10000000000000000000ull, "10000000000000000000");
    RequireJsonLiteral(10000000000000000001ull, "10000000000000000001");
  }

  SECTION("M support all common integer types") {
    RequireJsonLiteral(static_cast<signed char>(0xff), "-1");
    RequireJsonLiteral(static_cast<unsigned char>(0xff), "255");
    RequireJsonLiteral(static_cast<int8_t>(0xff), "-1");
    RequireJsonLiteral(static_cast<uint8_t>(0xff), "255");

    RequireJsonLiteral(static_cast<short>(0xff), "255");
    RequireJsonLiteral(static_cast<unsigned short>(0xff), "255");
    RequireJsonLiteral(static_cast<int16_t>(0xff), "255");
    RequireJsonLiteral(static_cast<uint16_t>(0xff), "255");

    RequireJsonLiteral(static_cast<int>(0xff), "255");
    RequireJsonLiteral(static_cast<unsigned int>(0xff), "255");
    RequireJsonLiteral(static_cast<long>(0xff), "255");
    RequireJsonLiteral(static_cast<unsigned long>(0xff), "255");
    RequireJsonLiteral(static_cast<int32_t>(0xff), "255");
    RequireJsonLiteral(static_cast<uint32_t>(0xff), "255");

    RequireJsonLiteral(static_cast<long long>(0xff), "255");
    RequireJsonLiteral(static_cast<unsigned long long>(0xff), "255");
    RequireJsonLiteral(static_cast<int64_t>(0xff), "255");
    RequireJsonLiteral(static_cast<uint64_t>(0xff), "255");

    RequireJsonLiteral(static_cast<size_t>(0xff), "255");
    RequireJsonLiteral(static_cast<intptr_t>(0xff), "255");
  }
}
