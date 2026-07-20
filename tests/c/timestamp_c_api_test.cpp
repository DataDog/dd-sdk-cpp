// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "datadog/timestamp.h"

TEST_CASE("dd_timestamp", "[unit][timestamp][c-api]") {
  SECTION("M be accurate nano count W initialized from dd_timestamp_ns") {
    dd_timestamp_t got = dd_timestamp_ns(1761233207897354033);
    REQUIRE(got == 1761233207897354033);
  }
  SECTION("M be accurate nano count W initialized from dd_timestamp_ms") {
    dd_timestamp_t got = dd_timestamp_ms(1761233207897);
    REQUIRE(got == 1761233207897000000);
  }
  SECTION("M be accurate second count W initialized from dd_timestamp_seconds") {
    dd_timestamp_t got = dd_timestamp_seconds(1761233207);
    REQUIRE(got == 1761233207000000000);
  }
}

TEST_CASE("dd_duration", "[unit][duration][c-api]") {
  SECTION("M be accurate nano count W initialized from dd_duration_ns") {
    dd_duration_t got = dd_duration_ns(738419026);
    REQUIRE(got == 738419026);
  }
  SECTION("M be accurate nano count W initialized from dd_duration_ms") {
    dd_duration_t got = dd_duration_ms(6173);
    REQUIRE(got == 6173000000);
  }
  SECTION("M be accurate nano count W initialized from dd_duration_seconds") {
    dd_duration_t got = dd_duration_seconds(47);
    REQUIRE(got == 47000000000);
  }
}
