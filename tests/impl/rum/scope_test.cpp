// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scope.hpp"

#include <catch2/catch_test_macros.hpp>
#include <map>

#include "datadog/rum.hpp"

#include "mock/clock.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("RumScopeDependencies::ShouldSampleSession", "[unit][rum]") {
  SECTION("M return true roughly 3 out of 4 times W sample rate is 75%") {
    // Given an ordinary RUM config with a session sample rate set to 75%
    RumConfig config = RumConfig("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    config.SetSessionSampleRate(75.0f);

    // And a set of RumScopeDependencies initialized from that config
    RumScopeDependencies deps(config, MockClock());

    // When we make a bunch of sampling decisions at a rate of 75%
    int num_sampled = 0;
    int num_ignored = 0;
    for (int i = 0; i < 1000; i++) {
      if (deps.ShouldSampleSession()) {
        num_sampled++;
      } else {
        num_ignored++;
      }
    }

    // Then some significant portion of sessions is sampled, and a smaller portion is
    // ignored
    REQUIRE(num_sampled > num_ignored);
    REQUIRE(num_sampled > 600);
    REQUIRE(num_ignored > 100);
  }

  SECTION("M always return true W sample rate is 100%") {
    // Given a RUM config with the default session sample rate, we should always
    // sample every session
    RumConfig config = RumConfig("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    RumScopeDependencies deps(config, MockClock());
    for (int i = 0; i < 200; i++) {
      const bool sampled = deps.ShouldSampleSession();
      REQUIRE(sampled == true);
    }
  }

  SECTION("M always return false W sample rate is 0%") {
    // Given a RUM config with session sample rate set to zero, we should never sample
    // any sessions
    RumConfig config = RumConfig("7d7b0b60-c062-4290-a7a4-a3701a7629c9");
    config.SetSessionSampleRate(0.0f);
    RumScopeDependencies deps(config, MockClock());
    for (int i = 0; i < 200; i++) {
      const bool sampled = deps.ShouldSampleSession();
      REQUIRE(sampled == false);
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
