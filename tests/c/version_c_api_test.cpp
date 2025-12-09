// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <cinttypes>
#include <cstring>
#include <vector>

#include "datadog/version.h"
#include "support/catch.hpp"

TEST_CASE("dd_get_version_info", "[unit][version][c-api]") {
  // Given a struct returned from dd_get_version_info()
  dd_version_info_t info = dd_get_version_info();

  SECTION("M have non-empty release") {
    REQUIRE(info.release != nullptr);
    REQUIRE(std::strlen(info.release) > 0);
  }

  SECTION("M have non-empty revision_id") {
    REQUIRE(info.revision_id != nullptr);
    REQUIRE(std::strlen(info.revision_id) > 0);
  }

  SECTION("M have non-empty artifact_name") {
    REQUIRE(info.artifact_name != nullptr);
    REQUIRE(std::strlen(info.artifact_name) > 0);
  }
}
