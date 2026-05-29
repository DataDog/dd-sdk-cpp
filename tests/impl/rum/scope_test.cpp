// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scope.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <map>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

#include "mock/clock.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("ShouldSampleSessionFromSeed - cross-SDK seed vectors", "[unit][rum]") {
  // These 10 vectors come from the iOS unit-test suite, originally imported from
  // dd-trace-go via the browser SDK. Any conforming implementation must reproduce them.
  struct Vector {
    uint64_t seed;
    float rate;
    bool expected;
  };
  const Vector vectors[] = {
      {5577006791947779410ULL, 94.0509f, true},
      {15352856648520921629ULL, 43.7714f, true},
      {3916589616287113937ULL, 68.6823f, true},
      {894385949183117216ULL, 30.0912f, true},
      {12156940908066221323ULL, 46.889f, true},
      {9828766684487745566ULL, 15.6519f, false},
      {4751997750760398084ULL, 81.364f, false},
      {11199607447739267382ULL, 38.0657f, false},
      {6263450610539110790ULL, 21.8553f, false},
      {1874068156324778273ULL, 36.0871f, false},
  };

  for (const auto& v : vectors) {
    REQUIRE(ShouldSampleSessionFromSeed(v.seed, v.rate) == v.expected);
  }
}

TEST_CASE("ShouldSampleSessionFromSeed - boundary conditions", "[unit][rum]") {
  SECTION("M always sample in W rate is 100%") {
    REQUIRE(ShouldSampleSessionFromSeed(0, 100.0f) == true);
    REQUIRE(ShouldSampleSessionFromSeed(0xFFFFFFFFFFFFFFFFULL, 100.0f) == true);
  }

  SECTION("M always sample out W rate is 0%") {
    REQUIRE(ShouldSampleSessionFromSeed(0, 0.0f) == false);
    REQUIRE(ShouldSampleSessionFromSeed(0xFFFFFFFFFFFFFFFFULL, 0.0f) == false);
  }

  SECTION("M sample in (fail-open) W seed is zero and rate > 0") {
    // A zero seed hashes to zero, which is below every positive threshold.
    REQUIRE(ShouldSampleSessionFromSeed(0, 1.0f) == true);
    REQUIRE(ShouldSampleSessionFromSeed(0, 50.0f) == true);
    REQUIRE(ShouldSampleSessionFromSeed(0, 99.9f) == true);
  }
}

TEST_CASE(
    "RumScopeDependencies::ShouldSampleSession - UUID integration", "[unit][rum]"
) {
  // UUID from the iOS integration test suite. Its node field (0xa522131ec48a) hashes
  // to ~50.68% of MAX_ID, so it falls just above a 50% threshold and below a 60% one.
  const auto session_id = UUID::Parse("c5b3c4ab-fa4a-4de9-8199-a522131ec48a");
  REQUIRE(session_id.has_value());

  SECTION("M not sample W rate is 50%") {
    RumConfig config("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    config.SetSessionSampleRate(50.0f);
    RumScopeDependencies deps(config, MockClock());
    REQUIRE(deps.ShouldSampleSession(*session_id) == false);
  }

  SECTION("M sample W rate is 60%") {
    RumConfig config("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    config.SetSessionSampleRate(60.0f);
    RumScopeDependencies deps(config, MockClock());
    REQUIRE(deps.ShouldSampleSession(*session_id) == true);
  }

  SECTION("M sample W rate is 80%") {
    RumConfig config("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    config.SetSessionSampleRate(80.0f);
    RumScopeDependencies deps(config, MockClock());
    REQUIRE(deps.ShouldSampleSession(*session_id) == true);
  }

  SECTION("M not sample W rate is 40%") {
    // Equivalent to combined rate 80 × 50/100 from the spec.
    RumConfig config("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    config.SetSessionSampleRate(40.0f);
    RumScopeDependencies deps(config, MockClock());
    REQUIRE(deps.ShouldSampleSession(*session_id) == false);
  }
}

TEST_CASE(
    "RumScopeDependencies::ShouldSampleSession - rate boundaries", "[unit][rum]"
) {
  SECTION("M always sample W rate is 100%") {
    RumConfig config("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    RumScopeDependencies deps(config, MockClock());
    for (int i = 0; i < 20; i++) {
      const UUID id = UUID::Random();
      REQUIRE(deps.ShouldSampleSession(id) == true);
    }
  }

  SECTION("M never sample W rate is 0%") {
    RumConfig config("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    config.SetSessionSampleRate(0.0f);
    RumScopeDependencies deps(config, MockClock());
    for (int i = 0; i < 20; i++) {
      const UUID id = UUID::Random();
      REQUIRE(deps.ShouldSampleSession(id) == false);
    }
  }

  SECTION("M produce consistent results for same UUID") {
    RumConfig config("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    config.SetSessionSampleRate(50.0f);
    RumScopeDependencies deps(config, MockClock());

    const UUID id = UUID::Random();
    const bool first = deps.ShouldSampleSession(id);
    for (int i = 0; i < 10; i++) {
      REQUIRE(deps.ShouldSampleSession(id) == first);
    }
  }
}

/**
 * Implementation of RumScope that implements Process() by ignoring the provided
 * command and simply returning the specified result value, while also recording the
 * number of calls made to Process() in an external map.
 */
struct MockScope {
  std::map<int, int>* process_calls_by_id;
  int id;
  RumScopeResult result;

  RumScopeResult Process(const RumCommand&) {
    auto found = process_calls_by_id->find(id);
    if (found != process_calls_by_id->end()) {
      found->second++;
    } else {
      process_calls_by_id->insert(std::make_pair(id, 1));
    }
    return result;
  }

  explicit MockScope(
      std::map<int, int>& in_process_calls_by_id, int in_id, RumScopeResult in_result
  )
      : process_calls_by_id(&in_process_calls_by_id), id(in_id), result(in_result) {}
};
