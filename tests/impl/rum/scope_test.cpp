// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scope.hpp"

#include <cstdint>
#include <limits>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

#include "mock/clock.hpp"
#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("ShouldSampleSessionFromSeed", "[unit][rum]") {
  SECTION("M produce true W given known test values that should be sampled") {
    // These values are replicated from DeterministicSamplerTests.swift, originally
    // derived from dd-trace-go via browser SDK sampler.spec.ts
    REQUIRE(ShouldSampleSessionFromSeed(5577006791947779410ull, 94.0509f) == true);
    REQUIRE(ShouldSampleSessionFromSeed(15352856648520921629ull, 43.7714f) == true);
    REQUIRE(ShouldSampleSessionFromSeed(3916589616287113937ull, 68.6823f) == true);
    REQUIRE(ShouldSampleSessionFromSeed(894385949183117216ull, 30.0912f) == true);
    REQUIRE(ShouldSampleSessionFromSeed(12156940908066221323ull, 46.889f) == true);
  }

  SECTION("M produce false W given known test values that should not be sampled") {
    REQUIRE(ShouldSampleSessionFromSeed(9828766684487745566ull, 15.6519f) == false);
    REQUIRE(ShouldSampleSessionFromSeed(4751997750760398084ull, 81.364f) == false);
    REQUIRE(ShouldSampleSessionFromSeed(11199607447739267382ull, 38.0657f) == false);
    REQUIRE(ShouldSampleSessionFromSeed(6263450610539110790ull, 21.8553f) == false);
    REQUIRE(ShouldSampleSessionFromSeed(1874068156324778273ull, 36.0871f) == false);
  }

  SECTION("M unconditionally return true W sample rate is 100%") {
    REQUIRE(ShouldSampleSessionFromSeed(0, 100.0f) == true);
    REQUIRE(
        ShouldSampleSessionFromSeed(std::numeric_limits<uint64_t>::max(), 100.0f) ==
        true
    );
  }

  SECTION("M unconditionally return false W sample rate is 0%") {
    REQUIRE(ShouldSampleSessionFromSeed(0, 0.0f) == false);
    REQUIRE(
        ShouldSampleSessionFromSeed(std::numeric_limits<uint64_t>::max(), 0.0f) == false
    );
  }

  SECTION("M sample in (fail-open) W seed is zero and rate > 0") {
    // A zero seed hashes to zero, which is below every positive threshold.
    REQUIRE(ShouldSampleSessionFromSeed(0, 1.0f) == true);
    REQUIRE(ShouldSampleSessionFromSeed(0, 50.0f) == true);
    REQUIRE(ShouldSampleSessionFromSeed(0, 99.9f) == true);
  }
}

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
