// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/platform/windows_timezone_mapping.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace datadog;

TEST_CASE("MapWindowsTimezoneToIANA", "[unit][platform-windows-timezone]") {
  SECTION("M return equivalent IANA timezone W input value is known Windows timezone") {
    REQUIRE(
        impl::platform::MapWindowsTimezoneToIANA("Pacific Standard Time") ==
        "America/Los_Angeles"
    );
    REQUIRE(
        impl::platform::MapWindowsTimezoneToIANA("Eastern Standard Time") ==
        "America/New_York"
    );
    REQUIRE(impl::platform::MapWindowsTimezoneToIANA("UTC") == "Etc/UTC");
    REQUIRE(
        impl::platform::MapWindowsTimezoneToIANA("GMT Standard Time") == "Europe/London"
    );
  }

  SECTION(
      "M return value unchanged W input value is not a recognized Windows timezone"
  ) {
    REQUIRE(
        impl::platform::MapWindowsTimezoneToIANA("Unknown Timezone") ==
        "Unknown Timezone"
    );
  }

  SECTION("M return empty string W input value is empty string") {
    REQUIRE(impl::platform::MapWindowsTimezoneToIANA("").empty());
  }
}
