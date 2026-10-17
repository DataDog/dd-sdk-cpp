// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/tracing/trace_id.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

using namespace datadog::impl;

TEST_CASE("TraceID", "[unit][tracing]") {
  SECTION("M compare IDs properly") {
    TraceID id(1, 2);
    TraceID sameId(1, 2);
    TraceID idWithDifferentHigh(3, 2);
    TraceID idWithDifferentLow(1, 4);
    TraceID idWithBothDifferent(42, 27);

    REQUIRE(id == sameId);
    REQUIRE_FALSE(id != sameId);

    REQUIRE(id != idWithDifferentHigh);
    REQUIRE(id != idWithDifferentLow);
    REQUIRE(id != idWithBothDifferent);
    REQUIRE_FALSE(id == idWithDifferentHigh);
    REQUIRE_FALSE(id == idWithDifferentLow);
    REQUIRE_FALSE(id == idWithBothDifferent);
  }

  SECTION("M Validate IDs") {
    REQUIRE_FALSE(TraceID(0, 0).IsValid());
    REQUIRE(TraceID(0, 1).IsValid());
    REQUIRE(TraceID(1, 0).IsValid());
    REQUIRE(TraceID(1, 1).IsValid());
    REQUIRE(TraceID(
                std::numeric_limits<uint64_t>::max(),
                std::numeric_limits<uint64_t>::max()
    )
                .IsValid());
  }

  SECTION("M Generate padded hex 32-characters representation") {
    REQUIRE(TraceID(0, 0).ToHexString() == "00000000000000000000000000000000");
    REQUIRE(TraceID(0, 1).ToHexString() == "00000000000000000000000000000001");
    REQUIRE(TraceID(0, 15).ToHexString() == "0000000000000000000000000000000f");
    REQUIRE(TraceID(0, 16).ToHexString() == "00000000000000000000000000000010");
    REQUIRE(TraceID(0, 123).ToHexString() == "0000000000000000000000000000007b");
    REQUIRE(TraceID(0, 123'456).ToHexString() == "0000000000000000000000000001e240");
    REQUIRE(
        TraceID(0, std::numeric_limits<uint64_t>::max()).ToHexString() ==
        "0000000000000000ffffffffffffffff"
    );

    // 128-bit values.
    REQUIRE(TraceID(1, 0).ToHexString() == "00000000000000010000000000000000");
    REQUIRE(TraceID(1, 1).ToHexString() == "00000000000000010000000000000001");
    REQUIRE(TraceID(15, 15).ToHexString() == "000000000000000f000000000000000f");
    REQUIRE(TraceID(16, 16).ToHexString() == "00000000000000100000000000000010");
    REQUIRE(TraceID(123, 123).ToHexString() == "000000000000007b000000000000007b");
    REQUIRE(
        TraceID(123'456, 123'456).ToHexString() == "000000000001e240000000000001e240"
    );
    REQUIRE(
        TraceID(
            std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::max()
        )
            .ToHexString() == "ffffffffffffffffffffffffffffffff"
    );
  }

  SECTION("M Generate low decimal representation") {
    REQUIRE(TraceID(0, 0).ToLowDecimalString() == "0");
    REQUIRE(TraceID(42, 1).ToLowDecimalString() == "1");
    REQUIRE(TraceID(120, 15).ToLowDecimalString() == "15");
    REQUIRE(TraceID(33, 16).ToLowDecimalString() == "16");
    REQUIRE(TraceID(0, 123).ToLowDecimalString() == "123");
    REQUIRE(TraceID(1024, 123'456).ToLowDecimalString() == "123456");
    REQUIRE(
        TraceID(0, std::numeric_limits<uint64_t>::max()).ToLowDecimalString() ==
        "18446744073709551615"
    );

    REQUIRE(
        TraceID(std::numeric_limits<uint64_t>::max(), 123'456).ToLowDecimalString() ==
        "123456"
    );
  }
}
