// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <cinttypes>
#include <vector>

#include "datadog/version.hpp"
#include "support/catch.hpp"

using namespace datadog;

TEST_CASE("GetVersionInfo", "[unit][version][cpp-api]") {
  // Given a struct returned from datadog::GetVersionInfo()
  VersionInfo info = GetVersionInfo();

  SECTION("M have non-empty release") { REQUIRE(info.release.size() > 0); }

  SECTION("M have non-empty revision_id") { REQUIRE(info.revision_id.size() > 0); }

  SECTION("M have non-empty artifact_name") { REQUIRE(info.artifact_name.size() > 0); }
}
