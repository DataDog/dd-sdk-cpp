// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/upload_util.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("GetIntakeOrigin", "[unit][core]") {
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

TEST_CASE("GetUserAgent", "[unit][core]") {
  SECTION("M concatenate all values into standard User-Agent format") {
    REQUIRE(
        GetUserAgent(
            "my-service", "1.0.1", "my-reporter", "2.0.2", "my-device", "my-os", "3.0.3"
        ) == "my-service/1.0.1 my-reporter/2.0.2 (my-device; my-os/3.0.3)"
    );
  }

  SECTION("M omit version suffixes W no version numbers specified") {
    REQUIRE(
        GetUserAgent("my-service", "", "my-reporter", "", "my-device", "my-os", "") ==
        "my-service my-reporter (my-device; my-os)"
    );
  }

  SECTION("M replace all whitespace characters with hyphens") {
    REQUIRE(
        GetUserAgent(
            "my service",
            "1.0 1",
            " my\treporter ",
            "2.0 \n 2",
            "my device",
            "my os",
            "3.0 3"
        ) == "my-service/1.0-1 -my-reporter-/2.0-2 (my-device; my-os/3.0-3)"
    );
  }
}

TEST_CASE("BuildDdTags", "[unit][core]") {
  SECTION("M produce comma-delimited key:val list W all required values provided") {
    const auto got = BuildDdTags("my-service", "", "prod", "2.0.0", "");
    REQUIRE(got == "service:my-service,env:prod,sdk_version:2.0.0");
  }

  SECTION("M include optional values W all values provided") {
    const auto got = BuildDdTags("my-service", "1.2.3", "prod", "2.0.0", "Debug");
    REQUIRE(
        got ==
        "service:my-service,version:1.2.3,env:prod,sdk_version:2.0.0,variant:Debug"
    );
  }

  SECTION("M strip commas and colons from values, leaving all other chars intact") {
    const auto got =
        BuildDdTags("My:Service !", "1,2,3", "prod:blue", "2\t0.0", "Debug\n?");
    REQUIRE(
        got ==
        "service:MyService !,version:123,env:prodblue,"
        "sdk_version:2\t0.0,variant:Debug\n?"
    );
  }

  SECTION("M concatenate additional tags W tail is non-empty") {
    const auto got =
        BuildDdTags("my-service", "", "prod", "2.0.0", "", "foo:value1,bar:value2");
    REQUIRE(
        got == "service:my-service,env:prod,sdk_version:2.0.0,foo:value1,bar:value2"
    );
  }
}
