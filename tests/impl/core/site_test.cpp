// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/site.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("GetIntakeOrigin", "[unit]") {
  SECTION("M return known datacenter origin W corresponding site is given") {
    REQUIRE(GetIntakeOrigin(Site::us1, "") == "https://browser-intake-datadoghq.com");
    REQUIRE(
        GetIntakeOrigin(Site::us3, "") == "https://browser-intake-us3-datadoghq.com"
    );
    REQUIRE(
        GetIntakeOrigin(Site::us5, "") == "https://browser-intake-us5-datadoghq.com"
    );
    REQUIRE(GetIntakeOrigin(Site::eu1, "") == "https://browser-intake-datadoghq.eu");
    REQUIRE(
        GetIntakeOrigin(Site::ap1, "") == "https://browser-intake-ap1-datadoghq.com"
    );
    REQUIRE(
        GetIntakeOrigin(Site::ap2, "") == "https://browser-intake-ap2-datadoghq.com"
    );
    REQUIRE(
        GetIntakeOrigin(Site::us1_fed, "") == "https://browser-intake-ddog-gov.com"
    );
  }

  SECTION("M override with custom endpoint W custom endpoint is given") {
    REQUIRE(
        GetIntakeOrigin(Site::us1, "http://127.0.0.1:5000") == "http://127.0.0.1:5000"
    );
    REQUIRE(GetIntakeOrigin(Site::us1, "https://localhost") == "https://localhost");
  }

  SECTION("M not override W custom endpoint is not a valid origin") {
    REQUIRE(
        GetIntakeOrigin(Site::us1, "gopher://192.168.0.1") ==
        "https://browser-intake-datadoghq.com"
    );
    REQUIRE(
        GetIntakeOrigin(Site::us1, "my-machine.local") ==
        "https://browser-intake-datadoghq.com"
    );
  }
}
