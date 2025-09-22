// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/scopes/application.hpp"

#include <catch2/catch_test_macros.hpp>

#include "datadog/rum.hpp"
#include "features/rum/context.hpp"
#include "mock/clock.hpp"

using namespace datadog;
using namespace datadog::impl;

class ApplicationFixture {
 protected:
  static constexpr const char* APPLICATION_ID = "a991ca10-4004-4004-4004-beefbeefbeef";

  RumConfig config;
  RumScopeDependencies deps;
  RumApplicationScope scope;

  MockClock clock;

 public:
  ApplicationFixture() : config(APPLICATION_ID), deps(config), scope(deps) {
    clock.FreezeAtMilliseconds(1700000000000);
  }

  RumCommandParams GetBaseParams() {
    return RumCommandParams(clock.Now(), ObjectAttribute(0), ObjectAttribute(0));
  }
};

TEST_CASE_METHOD(ApplicationFixture, "RumApplicationScope::Process", "[unit][rum]") {
  SECTION("M create new session W SDKInit is processed") {
    // Given a RumApplicationScope with no active session
    REQUIRE(scope.GetActiveSession() == std::nullopt);

    // When we process SDKInit
    scope.Process(RumCommand::SDKInit(GetBaseParams()));

    // Then a new session is created
    auto session_opt = scope.GetActiveSession();
    REQUIRE(session_opt.has_value());
    const RumSessionScope& session = *session_opt;

    // And the session is flagged as the very first session, with the default
    // 'UserAppLaunch' start precondition, and it remains active
    REQUIRE(session.IsInitialSession() == true);
    REQUIRE(session.GetSessionID() != UUID::Zero);
    REQUIRE(session.GetStartReason() == RumSessionPrecondition::UserAppLaunch);
    REQUIRE(session.GetEndReason() == std::nullopt);
  }

  SECTION("M end existing session W StopSession is processed") {
    // Given a RumApplicationScope with an active session
    scope.Process(RumCommand::SDKInit(GetBaseParams()));
    REQUIRE(scope.GetActiveSession());
    const UUID initial_session_id = (*scope.GetActiveSession()).get().GetSessionID();

    // When we process StopSession
    scope.Process(RumCommand::StopSession(GetBaseParams()));

    // Then we no longer have an active session
    REQUIRE(scope.GetActiveSession() == std::nullopt);

    // And our original session is still retained as the most recent session
    REQUIRE(scope.GetMostRecentSession());
    const RumSessionScope& prev_session = *scope.GetMostRecentSession();
    REQUIRE(prev_session.IsInitialSession() == true);
    REQUIRE(prev_session.GetSessionID() == initial_session_id);
    REQUIRE(prev_session.GetStartReason() == RumSessionPrecondition::UserAppLaunch);
    REQUIRE(prev_session.GetEndReason() == RumSessionScope::EndReason::Stopped);
  }

  SECTION("M start new session W any user interaction is processed after StopSession") {
    // Given a RumApplicationScope with an active session
    scope.Process(RumCommand::SDKInit(GetBaseParams()));
    REQUIRE(scope.GetActiveSession());
    const UUID initial_session_id = (*scope.GetActiveSession()).get().GetSessionID();

    // When we process StopSession
    scope.Process(RumCommand::StopSession(GetBaseParams()));

    // And then we subsequently process any command that represents user interaction
    scope.Process(RumCommand::UserInteraction(GetBaseParams()));

    // Then we once again have an active session, and it's distinct from the first
    REQUIRE(scope.GetActiveSession());
    const RumSessionScope& new_session = *scope.GetActiveSession();
    REQUIRE(new_session.IsInitialSession() == false);
    REQUIRE(new_session.GetSessionID() != initial_session_id);
    REQUIRE(new_session.GetStartReason() == RumSessionPrecondition::ExplicitStop);
    REQUIRE(new_session.GetEndReason() == std::nullopt);

    // And our 'most recent' session is one and the same with our new active session
    REQUIRE(scope.GetMostRecentSession());
    const RumSessionScope& most_recent_session = *scope.GetMostRecentSession();
    REQUIRE(most_recent_session.GetSessionID() == new_session.GetSessionID());
  }

  SECTION(
      "M end existing session and start new one W user interaction is processed after "
      "15m+ of inactivity"
  ) {
    // Given a RumApplicationScope with an active session
    scope.Process(RumCommand::SDKInit(GetBaseParams()));
    REQUIRE(scope.GetActiveSession());
    const UUID initial_session_id = (*scope.GetActiveSession()).get().GetSessionID();

    // When we wait 16 minutes, then process any command that represents user
    // interaction
    // TODO(RUM-11368): Replace 'UserInteraction' with 'StartView' so we can validate
    // that the new session processes the command
    clock.Tick(std::chrono::minutes(16));
    scope.Process(RumCommand::UserInteraction(GetBaseParams()));

    // Then we have an active session that's distinct from the first one
    REQUIRE(scope.GetActiveSession());
    const RumSessionScope& new_session = *scope.GetActiveSession();
    REQUIRE(new_session.IsInitialSession() == false);
    REQUIRE(new_session.GetSessionID() != initial_session_id);

    // And its start precondition is recorded as 'inactivity_timeout'
    REQUIRE(new_session.GetStartReason() == RumSessionPrecondition::InactivityTimeout);
    REQUIRE(new_session.GetEndReason() == std::nullopt);
  }

  SECTION("M end existing session and start new one W session duration is 4h+") {
    // Given a RumApplicationScope with an active session
    scope.Process(RumCommand::SDKInit(GetBaseParams()));
    REQUIRE(scope.GetActiveSession());
    const UUID initial_session_id = (*scope.GetActiveSession()).get().GetSessionID();

    // When we record user interactions at [T+14m, T+28m, ..., T+238m], a total of 17
    // times so that our last interaction is recorded at 3h58m into the session
    for (int i = 1; i <= 17; i++) {
      clock.Tick(std::chrono::minutes(14));
      scope.Process(RumCommand::UserInteraction(GetBaseParams()));
    }

    // Then as of 3h58m, our original session should still be active
    REQUIRE(scope.GetActiveSession());
    const RumSessionScope& initial_session = *scope.GetActiveSession();
    REQUIRE(initial_session.IsInitialSession() == true);
    REQUIRE(initial_session.GetSessionID() == initial_session_id);

    // Next: When we wait three minutes, then try to record a user interaction at 4h01m
    // TODO(RUM-11368): Replace 'UserInteraction' with 'StartView' so we can validate
    // that the new session processes the command
    clock.Tick(std::chrono::minutes(3));
    scope.Process(RumCommand::UserInteraction(GetBaseParams()));

    // Then we have an active session that's distinct from the first one
    REQUIRE(scope.GetActiveSession());
    const RumSessionScope& new_session = *scope.GetActiveSession();
    REQUIRE(new_session.IsInitialSession() == false);
    REQUIRE(new_session.GetSessionID() != initial_session_id);

    // And its start precondition is recorded as 'max_duration'
    REQUIRE(new_session.GetStartReason() == RumSessionPrecondition::MaxDuration);
    REQUIRE(new_session.GetEndReason() == std::nullopt);
  }
}

TEST_CASE("RumApplicationScope::PopulateContext", "[unit][rum]") {
  SECTION("M set application_id") {
    // Given a RumApplicationScope configured with a specific app ID
    RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
    RumScopeDependencies deps(config);
    RumApplicationScope scope(deps);

    // When we populate a RumContext from that application scope
    RumContext ctx;
    scope.PopulateContext(ctx);

    // Then its application_id value reflects the configured value
    REQUIRE(ctx.application_id == *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef"));
  }
}
