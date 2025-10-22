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
#include "features/rum/scopes/view.hpp"
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
  SessionFixture(bool is_session_sampled = true)
      : config(APPLICATION_ID),
        deps(config),
        parent(deps),
        scope(
            deps,
            parent,
            true,
            is_session_sampled,
            *UUID::Parse(SESSION_ID),
            RumSessionPrecondition::UserAppLaunch,
            platform::Timestamp{std::chrono::duration_cast<platform::Duration>(
                std::chrono::milliseconds{1700000000000}
            )},
            std::nullopt
        ) {
    clock.FreezeAtMilliseconds(1700000000000);
  }

  RumCommandParams GetBaseParams() { return RumCommandParams(clock.Now(), {}, {}); }
};

class NonSampledSessionFixture : public SessionFixture {
 public:
  static constexpr bool is_session_sampled = false;
  NonSampledSessionFixture() : SessionFixture(is_session_sampled) {};
};

TEST_CASE_METHOD(SessionFixture, "RumSessionScope::Process", "[unit][rum]") {
  SECTION("M remain open W user interaction is processed within limits") {
    // Given an active RumSessionScope
    REQUIRE(scope.IsInitialSession() == true);
    REQUIRE(scope.GetSessionID().ToString() == "5e551017-4114-4114-4114-beeeefbeeeef");
    REQUIRE(scope.GetStartReason() == RumSessionPrecondition::UserAppLaunch);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // When we process any user interaction
    RumScopeResult result = scope.Process(RumCommand::StopAction(GetBaseParams()));

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
    RumScopeResult result = scope.Process(RumCommand::StopAction(GetBaseParams()));

    // Then the scope is still open, as 7m does not exceed our inactivity timeout
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // Next: When we process any user interaction 14 minutes thereafter
    clock.Tick(std::chrono::minutes(14));
    result = scope.Process(RumCommand::StopAction(GetBaseParams()));

    // Then the result is the same, as 14m does not exceed our timeout either, and our
    // previous command refreshed the last-interaction timestamp
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // Next: When we wait a full 16 minutes before processing the next user interaction
    clock.Tick(std::chrono::minutes(16));
    result = scope.Process(RumCommand::StopAction(GetBaseParams()));

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
      const auto result = scope.Process(RumCommand::StopAction(GetBaseParams()));
      REQUIRE(result == RumScopeResult::RemainOpen);
      REQUIRE(scope.GetEndReason() == std::nullopt);
    }

    // Next: When we advance time to T+4h01m and process another user interaction
    clock.Tick(std::chrono::minutes(11));
    const auto result = scope.Process(RumCommand::StopAction(GetBaseParams()));

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
      const auto result = scope.Process(RumCommand::StopAction(GetBaseParams()));
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

  SECTION("M start new view W StartView is processed") {
    // Given a session with no views
    REQUIRE(scope.GetActiveView() == std::nullopt);

    // When we process StartView
    const auto result =
        scope.Process(RumCommand::StartView(GetBaseParams(), "view-a", "View A"));

    // Then the session has an active view scope that reflects the parameters configured
    // in the command
    REQUIRE(result == RumScopeResult::RemainOpen);
    auto view_opt = scope.GetActiveView();
    REQUIRE(view_opt.has_value());
    const RumViewScope& view = *view_opt;
    REQUIRE(view.IsActive() == true);
    REQUIRE(view.GetViewID() != UUID::Zero);
    REQUIRE(view.GetKey() == "view-a");
    REQUIRE(view.GetName() == "View A");
  }

  SECTION("M start new view W StartView is processed while another view is active") {
    // Given a session with an active view A
    REQUIRE(scope.GetActiveView() == std::nullopt);
    scope.Process(RumCommand::StartView(GetBaseParams(), "view-a", "View A"));
    REQUIRE(scope.GetActiveView()->get().GetKey() == "view-a");
    const UUID view_a_id = scope.GetActiveView()->get().GetViewID();

    // When we create another view B in response to a StartView command
    const auto result =
        scope.Process(RumCommand::StartView(GetBaseParams(), "view-b", "View B"));

    // Then the session's active view is now B
    REQUIRE(result == RumScopeResult::RemainOpen);
    auto view_opt = scope.GetActiveView();
    REQUIRE(view_opt.has_value());
    const RumViewScope& view = *view_opt;
    REQUIRE(view.IsActive() == true);
    REQUIRE(view.GetViewID() != view_a_id);
    REQUIRE(view.GetKey() == "view-b");
    REQUIRE(view.GetName() == "View B");
  }

  SECTION("M start new view W StartView has same key as active view") {
    // Given a session with an active view with key 'view-a'
    REQUIRE(scope.GetActiveView() == std::nullopt);
    scope.Process(RumCommand::StartView(GetBaseParams(), "view-a", "View A"));
    REQUIRE(scope.GetActiveView()->get().GetKey() == "view-a");
    const UUID initial_view_id = scope.GetActiveView()->get().GetViewID();

    // When we create another view, also 'view-a', in response to a StartView command
    const auto result =
        scope.Process(RumCommand::StartView(GetBaseParams(), "view-a", "View A"));

    // Then the session's active view changes to the new scope
    REQUIRE(result == RumScopeResult::RemainOpen);
    auto view_opt = scope.GetActiveView();
    REQUIRE(view_opt.has_value());
    const RumViewScope& view = *view_opt;
    REQUIRE(view.IsActive() == true);
    REQUIRE(view.GetViewID() != initial_view_id);
    REQUIRE(view.GetKey() == "view-a");
    REQUIRE(view.GetName() == "View A");
  }

  SECTION("M deactivate active view W StopView has matching key") {
    // Given a session with 'view-a' active
    scope.Process(RumCommand::StartView(GetBaseParams(), "view-a", "View A"));
    REQUIRE(scope.GetActiveView()->get().GetKey() == "view-a");

    // When we process a StopView command that targets 'view-a'
    const auto result = scope.Process(RumCommand::StopView(GetBaseParams(), "view-a"));

    // Then our session remains open, but it no longer has an active view
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetActiveView() == std::nullopt);
  }

  SECTION("M do nothing W StopView does not target any existing view") {
    // Given a session with 'view-a' active
    scope.Process(RumCommand::StartView(GetBaseParams(), "view-a", "View A"));
    REQUIRE(scope.GetActiveView()->get().GetKey() == "view-a");
    const UUID initial_view_id = scope.GetActiveView()->get().GetViewID();

    // When we process a StopView command that targets 'view-b'
    const auto result = scope.Process(RumCommand::StopView(GetBaseParams(), "view-b"));

    // Then our original view scope remains in place: nothing changes
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetActiveView().has_value());
    REQUIRE(scope.GetActiveView()->get().GetViewID() == initial_view_id);
  }

  // TODO(RUM-12202): Test deactivation of views with pending resources

  // TODO(RUM-12242): Test creation of ApplicationLaunch view

  // TODO(RUM-12247): Test creation of Background view
}

TEST_CASE_METHOD(
    NonSampledSessionFixture, "RumSessionScope::Process {non-sampled}", "[unit][rum]"
) {
  SECTION("M do nothing W StartView is processed") {
    // Given a session that isn't being sampled
    REQUIRE(scope.GetActiveView() == std::nullopt);

    // When we handle a StartView call
    auto result = scope.Process(RumCommand::StartView(GetBaseParams(), "foo", ""));

    // The session scope does not create a new view or otherwise update its state, as it
    // doesn't need to send any events
    REQUIRE(scope.GetActiveView() == std::nullopt);

    // And the session scope remains open
    REQUIRE(result == RumScopeResult::RemainOpen);
  }

  SECTION("M track user activity normally W StartView is processed") {
    // Given an active session that isn't being sampled
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // When we wait 10 minutes between StartView commands, the session remains open
    clock.Tick(std::chrono::minutes(10));
    auto result = scope.Process(RumCommand::StartView(GetBaseParams(), "foo", ""));
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);
    clock.Tick(std::chrono::minutes(10));
    result = scope.Process(RumCommand::StartView(GetBaseParams(), "foo", ""));
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // Next: When we wait 16 minutes and process another StartView
    clock.Tick(std::chrono::minutes(16));
    result = scope.Process(RumCommand::StartView(GetBaseParams(), "foo", ""));

    // Then the session is closed due to user inactivity
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(
        scope.GetEndReason().value() ==
        RumSessionScope::EndReason::TimedOutDueToInactivity
    );
  }

  SECTION("M apply duration limit normally W StartView is processed") {
    // Given an active session that isn't being sampled
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // When we process a StartView call every 10 minutes for 230 minutes, to keep the
    // session active at T+3h50m
    for (int i = 1; i <= 23; i++) {
      clock.Tick(std::chrono::minutes(10));

      // Then every such interaction is accepted and keeps the session open
      auto result = scope.Process(RumCommand::StartView(GetBaseParams(), "foo", ""));
      REQUIRE(result == RumScopeResult::RemainOpen);
      REQUIRE(scope.GetEndReason() == std::nullopt);
    }

    // Next: When we advance time to T+4h01m and process another user interaction
    clock.Tick(std::chrono::minutes(11));
    auto result = scope.Process(RumCommand::StartView(GetBaseParams(), "foo", ""));

    // Then the session is closed due to excessive duration
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(
        scope.GetEndReason().value() == RumSessionScope::EndReason::ExceededMaxDuration
    );
  }

  SECTION("M handle StopSession normally despite not being sampled") {
    // Given an active session that isn't being sampled
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // When we handle a StopSession call
    auto result = scope.Process(RumCommand::StopSession(GetBaseParams()));

    // Then the session is closed as usual: the command is heeded even though the
    // session isn't sampled
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(scope.GetEndReason().value() == RumSessionScope::EndReason::Stopped);
  }
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
        platform::Timestamp{},
        std::nullopt
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
        platform::Timestamp{},
        std::nullopt
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
        platform::Timestamp{},
        std::nullopt
    );

    // When the session ends for any reason
    scope.Process(
        RumCommand::StopSession(RumCommandParams(platform::Timestamp{}, {}, {}))
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
