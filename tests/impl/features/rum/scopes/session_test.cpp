// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/scopes/session.hpp"

#include <catch2/catch_test_macros.hpp>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"
#include "features/rum/context.hpp"
#include "features/rum/scopes/application.hpp"
#include "mock/clock.hpp"

using namespace datadog;
using namespace datadog::impl;

class SessionFixture {
 protected:
  static constexpr const char* APPLICATION_ID = "a991ca10-4004-4004-4004-beefbeefbeef";
  static constexpr const char* SESSION_ID = "5e551017-4114-4114-4114-beeeefbeeeef";

  RumConfig config;
  RumScopeDependencies deps;
  RumApplicationScope parent;
  RumSessionScope scope;

  MockClock clock;

 public:
  SessionFixture()
      : config(APPLICATION_ID),
        deps(config),
        parent(deps),
        scope(
            deps,
            parent,
            true,
            true,
            *UUID::Parse(SESSION_ID),
            RumSessionPrecondition::UserAppLaunch,
            platform::Timestamp{std::chrono::duration_cast<platform::Duration>(
                std::chrono::milliseconds{1700000000000}
            )}
        ) {
    clock.FreezeAtMilliseconds(1700000000000);
  }

  RumCommandParams GetBaseParams() {
    return RumCommandParams(clock.Now(), ObjectAttribute(0), ObjectAttribute(0));
  }
};

TEST_CASE_METHOD(SessionFixture, "RumSessionScope::Process", "[unit][rum]") {
  SECTION("M remain open W user interaction is processed within limits") {
    // Given an active RumSessionScope
    REQUIRE(scope.IsInitialSession() == true);
    REQUIRE(scope.GetSessionID().ToString() == "5e551017-4114-4114-4114-beeeefbeeeef");
    REQUIRE(scope.GetStartReason() == RumSessionPrecondition::UserAppLaunch);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // When we process any user interaction
    RumScopeResult result = scope.Process(RumCommand::UserInteraction(GetBaseParams()));

    // Then the command is processed and the session scope remains open
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);
  }

  SECTION(
      "M close with TimedOutDueToInactivity W user interaction is processed after "
      "15m+ of inactivity"
  ) {
    // When we process any user interaction after 7 minutes
    clock.Tick(std::chrono::minutes(7));
    RumScopeResult result = scope.Process(RumCommand::UserInteraction(GetBaseParams()));

    // Then the scope is still open, as 7m does not exceed our inactivity timeout
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // Next: When we process any user interaction 14 minutes thereafter
    clock.Tick(std::chrono::minutes(14));
    result = scope.Process(RumCommand::UserInteraction(GetBaseParams()));

    // Then the result is the same, as 14m does not exceed our timeout either, and our
    // previous command refreshed the last-interaction timestamp
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // Next: When we wait a full 16 minutes before processing the next user interaction
    clock.Tick(std::chrono::minutes(16));
    result = scope.Process(RumCommand::UserInteraction(GetBaseParams()));

    // Then our session scope is closed and the command is not handled
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(
        scope.GetEndReason() == RumSessionScope::EndReason::TimedOutDueToInactivity
    );
  }

  SECTION(
      "M close with ExceededMaxDuration W user interaction is processed 4h+ after start"
  ) {
    // When we process a user interaction every 10 minutes for 230 minutes, to keep the
    // session active at T+3h50m
    for (int i = 1; i <= 23; i++) {
      clock.Tick(std::chrono::minutes(10));

      // Then every such interaction is accepted and keeps the session open
      const auto result = scope.Process(RumCommand::UserInteraction(GetBaseParams()));
      REQUIRE(result == RumScopeResult::RemainOpen);
      REQUIRE(scope.GetEndReason() == std::nullopt);
    }

    // Next: When we advance time to T+4h01m and process another user interaction
    clock.Tick(std::chrono::minutes(11));
    const auto result = scope.Process(RumCommand::UserInteraction(GetBaseParams()));

    // Then our session scope is closed and the command is not handled
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(scope.GetEndReason() == RumSessionScope::EndReason::ExceededMaxDuration);
  }

  SECTION("M close with Stopped W StopSession is processed") {
    // When we process StopSession
    const auto result = scope.Process(RumCommand::StopSession(GetBaseParams()));

    // Then the scope is explicitly closed
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(scope.GetEndReason() == RumSessionScope::EndReason::Stopped);
  }

  SECTION(
      "M close with TimedOutDueToInactivity W StopSession is processed after inactivity"
  ) {
    // When we process StopSession after 15m+ of inactivity
    clock.Tick(std::chrono::minutes(16));
    const auto result = scope.Process(RumCommand::StopSession(GetBaseParams()));

    // Then the scope is closed, but the inactivity takes precendence over the explicit
    // stop call
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(
        scope.GetEndReason() == RumSessionScope::EndReason::TimedOutDueToInactivity
    );
  }

  SECTION(
      "M close with ExceededMaxDuration W StopSession is processed beyond max duration"
  ) {
    // When we process StopSession at T+4h01m since session start
    for (int i = 1; i <= 23; i++) {
      clock.Tick(std::chrono::minutes(10));
      const auto result = scope.Process(RumCommand::UserInteraction(GetBaseParams()));
      REQUIRE(result == RumScopeResult::RemainOpen);
      REQUIRE(scope.GetEndReason() == std::nullopt);
    }
    clock.Tick(std::chrono::minutes(11));
    const auto result = scope.Process(RumCommand::StopSession(GetBaseParams()));

    // Then the scope is closed, but the excessive duration takes precendence over the
    // explicit stop call
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(scope.GetEndReason() == RumSessionScope::EndReason::ExceededMaxDuration);
  }

  // TODO(RUM-11368): Test view creation commands

  // TODO(RUM-11368): Verify that view creation is ignored when session not sampled
}

TEST_CASE("RumSessionScope::PopulateContext", "[unit][rum]") {
  SECTION("M set application_id and session_id W session is active") {
    // Given a RumApplicationScope configured with a specific app ID
    RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
    RumScopeDependencies deps(config);
    RumApplicationScope application_scope(deps);

    // And a RumSessionScope with a specific session ID
    const bool is_initial_session = true;
    const bool is_sampled = true;
    const UUID session_id = *UUID::Parse("689d0d6c-c716-4eed-b449-0df936a615f8");
    RumSessionScope scope(
        deps,
        application_scope,
        is_initial_session,
        is_sampled,
        session_id,
        RumSessionPrecondition::UserAppLaunch,
        platform::Timestamp{}
    );

    // When we populate a RumContext from the session scope
    RumContext ctx;
    scope.PopulateContext(ctx);

    // Then it has all expected values
    REQUIRE(ctx.application_id == *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef"));
    REQUIRE(ctx.session_id == session_id);
    REQUIRE(ctx.session_is_sampled == true);
    REQUIRE(ctx.session_is_active == true);
    REQUIRE(ctx.session_precondition == RumSessionPrecondition::UserAppLaunch);
  }

  SECTION(
      "M retain basic session info but set is_sampled false W session not sampled"
  ) {
    // Given a RumApplicationScope configured with a specific app ID
    RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
    RumScopeDependencies deps(config);
    RumApplicationScope application_scope(deps);

    // And a RumSessionScope that's not sampled
    const bool is_initial_session = true;
    const bool is_sampled = false;
    const UUID session_id = *UUID::Parse("689d0d6c-c716-4eed-b449-0df936a615f8");
    RumSessionScope scope(
        deps,
        application_scope,
        is_initial_session,
        is_sampled,
        session_id,
        RumSessionPrecondition::BackgroundLaunch,
        platform::Timestamp{}
    );

    // When we populate a RumContext from the session scope
    RumContext ctx;
    scope.PopulateContext(ctx);

    // Then session_is_sampled is false; all other session info is recorded faithfully
    REQUIRE(ctx.application_id == *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef"));
    REQUIRE(ctx.session_id == session_id);
    REQUIRE(ctx.session_is_sampled == false);
    REQUIRE(ctx.session_is_active == true);
    REQUIRE(ctx.session_precondition == RumSessionPrecondition::BackgroundLaunch);
  }

  SECTION("M retain session_id with active flag false W session is no longer active") {
    // Given an application with an initial session
    RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
    RumScopeDependencies deps(config);
    RumApplicationScope application_scope(deps);
    const UUID session_id = *UUID::Parse("689d0d6c-c716-4eed-b449-0df936a615f8");
    const bool is_initial_session = true;
    const bool is_sampled = true;
    RumSessionScope scope(
        deps,
        application_scope,
        is_initial_session,
        is_sampled,
        session_id,
        RumSessionPrecondition::InactivityTimeout,
        platform::Timestamp{}
    );

    // When the session ends for any reason
    scope.Process(
        RumCommand::StopSession(RumCommandParams(
            platform::Timestamp{}, ObjectAttribute(0), ObjectAttribute(0)
        ))
    );

    // And we then populate a RumContext from the session scope
    RumContext ctx;
    scope.PopulateContext(ctx);

    // Then it remembers the details of the last active session, but reports that the
    // session is no longer active
    REQUIRE(ctx.session_id == session_id);
    REQUIRE(ctx.session_is_sampled == true);
    REQUIRE(ctx.session_is_active == false);
    REQUIRE(ctx.session_precondition == RumSessionPrecondition::InactivityTimeout);
  }
}
