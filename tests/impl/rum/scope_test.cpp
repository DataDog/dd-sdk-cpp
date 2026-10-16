// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scope.hpp"

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

#include "mock/clock.hpp"
#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("RumScopeDependencies::ShouldSampleSession", "[unit][rum]") {
  // Given UUID from the iOS integration test suite, whose seed (0xa522131ec48a) hashes
  // to ~50.68% of MAX_ID, falling just above a 50% threshold
  const UUID session_id = *UUID::Parse("c5b3c4ab-fa4a-4de9-8199-a522131ec48a");

  // And a RumConfig whose sample rate we can modify
  RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
  MockClock clock;
  auto make_deps = [&]() { return RumScopeDependencies(config, clock); };

  SECTION("M not sample W rate is below ~50.68") {
    auto rate = GENERATE(10.0f, 20.0f, 30.0f, 40.0f, 50.0f);
    config.SetSessionSampleRate(rate);
    auto deps = make_deps();
    REQUIRE(deps.ShouldSampleSession(session_id) == false);
  }

  SECTION("M sample W rate is above ~50.68") {
    auto rate = GENERATE(50.7f, 60.0f, 70.0f, 80.0f, 90.0f);
    config.SetSessionSampleRate(rate);
    auto deps = make_deps();
    REQUIRE(deps.ShouldSampleSession(session_id) == true);
  }

  SECTION("M never sample W rate is 0%") {
    config.SetSessionSampleRate(0.0f);
    auto deps = make_deps();
    for (int i = 0; i < 20; i++) {
      const UUID id = UUID::Random();
      REQUIRE(deps.ShouldSampleSession(id) == false);
    }
  }

  SECTION("M always sample W rate is 100%") {
    auto deps = make_deps();
    for (int i = 0; i < 20; i++) {
      const UUID id = UUID::Random();
      REQUIRE(deps.ShouldSampleSession(id) == true);
    }
  }
}
