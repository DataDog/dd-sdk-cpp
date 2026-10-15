// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/tracing/span_id.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

using namespace datadog::impl;

TEST_CASE("SpanID", "[unit][tracing]") {
  SECTION("M compare IDs properly") {
    SpanID id(1);
    SpanID sameId(1);
    SpanID otherID(42);

    REQUIRE(id == sameId);
    REQUIRE_FALSE(id != sameId);

    REQUIRE(id != otherID);
    REQUIRE_FALSE(id == otherID);
  }

  SECTION("M Validate IDs") {
    REQUIRE_FALSE(SpanID(0).IsValid());
    REQUIRE(SpanID(1).IsValid());
    REQUIRE(SpanID(std::numeric_limits<uint64_t>::max()).IsValid());
  }

  SECTION("M Generate hex string representations") {
    REQUIRE(SpanID(0).ToHexString() == "0000000000000000");
    REQUIRE(SpanID(1).ToHexString() == "0000000000000001");
    REQUIRE(SpanID(15).ToHexString() == "000000000000000f");
    REQUIRE(SpanID(16).ToHexString() == "0000000000000010");
    REQUIRE(SpanID(123).ToHexString() == "000000000000007b");
    REQUIRE(SpanID(123'456).ToHexString() == "000000000001e240");
    REQUIRE(
        SpanID(std::numeric_limits<uint64_t>::max()).ToHexString() == "ffffffffffffffff"
    );
  }

  SECTION("M Generate decimal string representations") {
    REQUIRE(SpanID(0).ToDecimalString() == "0");
    REQUIRE(SpanID(1).ToDecimalString() == "1");
    REQUIRE(SpanID(15).ToDecimalString() == "15");
    REQUIRE(SpanID(16).ToDecimalString() == "16");
    REQUIRE(SpanID(123).ToDecimalString() == "123");
    REQUIRE(SpanID(123'456).ToDecimalString() == "123456");
    REQUIRE(
        SpanID(std::numeric_limits<uint64_t>::max()).ToDecimalString() ==
        "18446744073709551615"
    );
  }
}
