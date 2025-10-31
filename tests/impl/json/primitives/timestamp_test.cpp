// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <limits>

#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("timestamp JSON serialization", "[unit][json]") {
  SECTION("M render valid ISO-8601 timestamp as JSON string") {
    auto test = [](Duration nanos, std::string_view want) {
      RequireJsonLiteral(Timestamp(nanos), want);
    };

    // Unit increments
    test(std::chrono::nanoseconds(0), "\"1970-01-01T00:00:00.000Z\"");
    test(std::chrono::microseconds(1), "\"1970-01-01T00:00:00.000Z\"");
    test(std::chrono::milliseconds(1), "\"1970-01-01T00:00:00.001Z\"");
    test(std::chrono::milliseconds(10), "\"1970-01-01T00:00:00.010Z\"");
    test(std::chrono::milliseconds(100), "\"1970-01-01T00:00:00.100Z\"");
    test(std::chrono::seconds(1), "\"1970-01-01T00:00:01.000Z\"");
    test(std::chrono::minutes(1), "\"1970-01-01T00:01:00.000Z\"");
    test(std::chrono::hours(1), "\"1970-01-01T01:00:00.000Z\"");
    test(std::chrono::hours(24), "\"1970-01-02T00:00:00.000Z\"");
    test(std::chrono::hours(24 * 31), "\"1970-02-01T00:00:00.000Z\"");
    test(std::chrono::hours(24 * 365), "\"1971-01-01T00:00:00.000Z\"");

    // Pre-1970 timestamps
    test(std::chrono::seconds(-1), "\"1969-12-31T23:59:59.000Z\"");
    test(std::chrono::seconds(-2208988800), "\"1900-01-01T00:00:00.000Z\"");

    // Leap day
    test(std::chrono::milliseconds(68239266580), "\"1972-02-29T19:21:06.580Z\"");

    // Y2K
    test(std::chrono::nanoseconds(946684799999999999), "\"1999-12-31T23:59:59.999Z\"");
    test(std::chrono::nanoseconds(946684800000000000), "\"2000-01-01T00:00:00.000Z\"");

    // INT32_MAX and INT32_MIN
    test(
        std::chrono::nanoseconds(std::numeric_limits<int64_t>::max()),
        "\"2262-04-11T23:47:16.854Z\""
    );
    test(
        std::chrono::nanoseconds(std::numeric_limits<int64_t>::min()),
        "\"1677-09-21T00:12:43.145Z\""
    );
  }
}
