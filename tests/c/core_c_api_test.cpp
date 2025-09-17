// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <thread>
#include <vector>

#include "datadog/core.h"

TEST_CASE("dd_core null safety", "[unit][core][c-api]") {
  SECTION("M safely do nothing W target object is null") {
    REQUIRE(dd_core_create(nullptr) == nullptr);
    dd_core_destroy(nullptr);
    REQUIRE(dd_core_start(nullptr) == false);
    dd_core_stop(nullptr);
  }
}
