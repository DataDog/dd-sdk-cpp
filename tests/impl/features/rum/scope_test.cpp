// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/scope.hpp"

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

/** Mock RumScope that closes immediately and tracks when its destructor is called. */
struct DtorScope {
  bool* dtor_called;
  explicit DtorScope(bool* in_dtor_called) : dtor_called(in_dtor_called) {}
  ~DtorScope() { *dtor_called = true; }
  RumScopeResult Process(const RumCommand&) { return RumScopeResult::Close; }
};

TEST_CASE("ScopeArray::Push", "[unit][rum]") {
  SECTION("M add scopes to the underlying vector") {
    // Given an empty array of scopes
    ScopeArray<MockScope> scopes;
    REQUIRE(scopes.items.empty());

    // When we call Push() with the arguments to our scope type's constructor
    std::map<int, int> process_calls_by_id;
    scopes.Push(process_calls_by_id, 42, RumScopeResult::RemainOpen);

    // Then our array now contains a scope object initialized from those values
    REQUIRE(scopes.items.size() == 1);
    REQUIRE(scopes.items.front().id == 42);
    REQUIRE(scopes.items.front().result == RumScopeResult::RemainOpen);

    // And no commands have been processed
    REQUIRE(process_calls_by_id.empty());
  }
}

TEST_CASE("ScopeArray::Propagate", "[unit][rum]") {
  SECTION("M call Process() on all scopes") {
    // Given an array of two scopes
    ScopeArray<MockScope> scopes;
    std::map<int, int> process_calls_by_id;
    scopes.Push(process_calls_by_id, 0, RumScopeResult::RemainOpen);
    scopes.Push(process_calls_by_id, 1, RumScopeResult::RemainOpen);

    // When we propagate a command to all scopes in our array
    scopes.Propagate(RumCommand::SDKInit(RumCommandParams({}, {}, {})));

    // Then Process() is called once for each scope in the array
    REQUIRE(process_calls_by_id.size() == 2);
    REQUIRE(process_calls_by_id[0] == 1);
    REQUIRE(process_calls_by_id[1] == 1);
  }

  SECTION("M remove scopes W Process() returns Close") {
    // Given an array of two scopes, with scope 100 configured to close on the first
    // command it sees
    ScopeArray<MockScope> scopes;
    std::map<int, int> process_calls_by_id;
    scopes.Push(process_calls_by_id, 100, RumScopeResult::Close);
    scopes.Push(process_calls_by_id, 200, RumScopeResult::RemainOpen);
    REQUIRE(scopes.items.size() == 2);

    // When we propagate a command to all scopes in our array
    scopes.Propagate(RumCommand::SDKInit(RumCommandParams({}, {}, {})));

    // Then scope 100 is closed, leaving scope 200 as the only item left in our array
    REQUIRE(scopes.items.size() == 1);
    REQUIRE(scopes.items.front().id == 200);

    // Next: When we propagate a second command
    scopes.Propagate(RumCommand::StopSession(RumCommandParams({}, {}, {})));

    // Then we still only have scope 200 in the array
    REQUIRE(scopes.items.size() == 1);
    REQUIRE(scopes.items.front().id == 200);

    // And Process() calls are recorded once for scope 100 and twice for scope 200
    REQUIRE(process_calls_by_id.size() == 2);
    REQUIRE(process_calls_by_id[100] == 1);
    REQUIRE(process_calls_by_id[200] == 2);
  }

  SECTION("M call scope destructor W scope is removed") {
    // Given a ScopeArray with a single scope
    bool dtor_called = false;
    ScopeArray<DtorScope> scopes;
    scopes.Push(&dtor_called);
    REQUIRE(scopes.items.size() == 1);
    REQUIRE(dtor_called == false);

    // When we process a command that results in the scope being closed
    scopes.Propagate(RumCommand::StopSession(RumCommandParams({}, {}, {})));

    // Then the scope object's destructor is called
    REQUIRE(dtor_called == true);

    // And no scopes remain in the array
    REQUIRE(scopes.items.empty());
  }
}
