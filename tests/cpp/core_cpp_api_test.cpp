// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "datadog/core.hpp"

using namespace datadog;

TEST_CASE("Core null safety", "[unit][core][cpp-api]") {
  SECTION("M safely do nothing W this wraps nullptr") {
    // Given a CoreConfig that lacks required parameters
    // TODO: Update CoreConfig with a more well-formed interface
    CoreConfig config{};

    // When we create a Core from that config
    auto core = Core::Create(config);

    // Then we get a valid object that handles all member functions calls as a no-op
    REQUIRE(core != nullptr);
    REQUIRE(core->Start() == false);
    core->Stop();
  }
}
