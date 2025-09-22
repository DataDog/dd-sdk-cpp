// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/feature_id.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace datadog::impl;

TEST_CASE("CreateFeatureId", "[unit]") {
  SECTION("M encode standard FourCC W given four ASCII bytes") {
    // Should encode with leftmost character as least significant byte
    REQUIRE(CreateFeatureId("ABCD") == 0x44434241);

    REQUIRE(CreateFeatureId("TEST") == 0x54534554);
    REQUIRE(CreateFeatureId("LOGS") == 0x53474f4C);
    REQUIRE(CreateFeatureId("1234") == 0x34333231);
    REQUIRE(CreateFeatureId("L\0GS") == 0x5347004C);
    REQUIRE(CreateFeatureId("\0\0\0\0") == 0x00000000);
    REQUIRE(CreateFeatureId("A1@#") == 0x23403141);
  }
}
