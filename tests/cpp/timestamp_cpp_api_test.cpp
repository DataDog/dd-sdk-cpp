// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "datadog/timestamp.hpp"

using namespace datadog;

TEST_CASE("Timestamp", "[unit][timestamp][cpp-api]") {
  SECTION("M be accurate nano count W initialized from std::chrono::nanoseconds") {
    Timestamp got(std::chrono::nanoseconds(1761233207897354033));
    REQUIRE(got.time_since_epoch().count() == 1761233207897354033);
  }
  SECTION("M be accurate nano count W initialized from std::chrono::milliseconds") {
    Timestamp got(std::chrono::milliseconds(1761233207897));
    REQUIRE(got.time_since_epoch().count() == 1761233207897000000);
  }
  SECTION("M be accurate second count W initialized from std::chrono::seconds") {
    Timestamp got(std::chrono::seconds(1761233207));
    REQUIRE(got.time_since_epoch().count() == 1761233207000000000);
  }
}
