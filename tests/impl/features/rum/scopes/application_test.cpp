// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/rum/scopes/application.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <functional>
#include <string_view>

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

  RumCommandParams GetBaseParams() { return RumCommandParams(clock.Now(), {}, {}); }
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

  SECTION("M start new session W user interaction is processed after StopSession") {
    // Given a RumApplicationScope with an active session
    scope.Process(RumCommand::SDKInit(GetBaseParams()));
    REQUIRE(scope.GetActiveSession());
    const UUID initial_session_id = (*scope.GetActiveSession()).get().GetSessionID();

    // When we process StopSession
    scope.Process(RumCommand::StopSession(GetBaseParams()));

    // And then we subsequently process a command like AddAction
    // (Note that the '{on session refresh}' tests below validate a more exhaustive
    // range of command types)
    scope.Process(RumCommand::AddAction(GetBaseParams()));

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

  SECTION("M do nothing W StopSession is processed after StopSession") {
    // Given a RumApplicationScope that's received StopSession, and therefore has no
    // active session
    scope.Process(RumCommand::SDKInit(GetBaseParams()));
    REQUIRE(scope.GetActiveSession());
    const UUID initial_session_id = (*scope.GetActiveSession()).get().GetSessionID();
    scope.Process(RumCommand::StopSession(GetBaseParams()));
    REQUIRE(scope.GetActiveSession() == std::nullopt);

    // When we process an additional StopSession command in our already-stopped session
    // (Note that the '{on session refresh}' tests below validate a more exhaustive
    // range of command types)
    scope.Process(RumCommand::StopSession(GetBaseParams()));

    // Then nothing happens: we still have no active session
    REQUIRE(scope.GetActiveSession() == std::nullopt);

    // And our original session is still retained as the most recent session
    REQUIRE(scope.GetMostRecentSession());
    const RumSessionScope& prev_session = *scope.GetMostRecentSession();
    REQUIRE(prev_session.IsInitialSession() == true);
    REQUIRE(prev_session.GetSessionID() == initial_session_id);
    REQUIRE(prev_session.GetStartReason() == RumSessionPrecondition::UserAppLaunch);
    REQUIRE(prev_session.GetEndReason() == RumSessionScope::EndReason::Stopped);
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
    clock.Tick(std::chrono::minutes(16));
    scope.Process(RumCommand::StopAction(GetBaseParams()));

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
      scope.Process(RumCommand::StopAction(GetBaseParams()));
    }

    // Then as of 3h58m, our original session should still be active
    REQUIRE(scope.GetActiveSession());
    const RumSessionScope& initial_session = *scope.GetActiveSession();
    REQUIRE(initial_session.IsInitialSession() == true);
    REQUIRE(initial_session.GetSessionID() == initial_session_id);

    // Next: When we wait three minutes, then try to record a user interaction at 4h01m
    clock.Tick(std::chrono::minutes(3));
    scope.Process(RumCommand::StopAction(GetBaseParams()));

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

/**
 * Fixture that sets up a RumApplicationScope with an active session and an active view,
 * so that we can test the logic used to determine whether the last active view should
 * be recreated on session refresh.
 */
class ViewTransferFixture {
 protected:
  static constexpr const char* APPLICATION_ID = "a991ca10-4004-4004-4004-beefbeefbeef";

  RumConfig config;
  RumScopeDependencies deps;
  RumApplicationScope scope;

  MockClock clock;

  // Recorded details of the session and view we started with
  UUID initial_session_id;
  UUID initial_view_id;
  std::string initial_view_key;
  std::string initial_view_name;
  Timestamp initial_view_started_at{};

  // Individual test scenarios
  enum class State {
    Initial,                  // At T+0 with initial session and view
    ExpiredDueToInactivity,   // At T+16m, next command will trigger expiration
    ExpiredDueToMaxDuration,  // At T+4h01m, next command will trigger expiration
    ExplicitlyStopped         // At T+5m, after StopSession, with no active session
  };
  State state{State::Initial};

 public:
  ViewTransferFixture() : config(APPLICATION_ID), deps(config), scope(deps) {
    clock.FreezeAtMilliseconds(1700000000000);

    // Issue SDKInit to create an initial session
    scope.Process(RumCommand::SDKInit(GetBaseParams()));
    auto session_opt = scope.GetActiveSession();
    REQUIRE(session_opt.has_value());
    const RumSessionScope& session = session_opt->get();

    // Create a new view with key 'foo'
    scope.Process(RumCommand::StartView(GetBaseParams(), "foo", "Foo"));
    auto view_opt = session.GetActiveView();
    REQUIRE(view_opt.has_value());
    const RumViewScope& view = view_opt->get();

    // Record initial session and view details for easy comparison in test cases
    initial_session_id = session.GetSessionID();
    initial_view_id = view.GetViewID();
    initial_view_key = view.GetKey();
    initial_view_name = view.GetName();
    initial_view_started_at = view.GetStartedAt();

    REQUIRE(initial_session_id != UUID::Zero);
    REQUIRE(initial_view_id != UUID::Zero);
    REQUIRE(!initial_view_key.empty());
    REQUIRE(!initial_view_name.empty());
    const bool started_now = initial_view_started_at == clock.Now();
    REQUIRE(started_now);
  }

  void EnterState(State new_state) {
    // We should only call EnterState() once per test
    REQUIRE(state == State::Initial);

    // Adopt the new enum value, then adjust our clock and application scope as needed
    state = new_state;
    switch (state) {
      case State::Initial:
        // Initial state requires no extra setup
        RequireInitialView();
        return;
      case State::ExpiredDueToInactivity:
        // Advance past the inactivity timeout: next command will trigger expiration
        // and refresh
        clock.Tick(std::chrono::minutes(16));
        RequireInitialView();
        return;
      case State::ExpiredDueToMaxDuration:
        // Advance right up to the threshold of the max session duration, periodically
        // triggering user interactions to keep the session alive
        for (int i = 1; i <= 17; i++) {
          // User interactions recorded at [T+14m, T+28m, ..., T+238m]
          clock.Tick(std::chrono::minutes(14));
          scope.Process(RumCommand::StopAction(GetBaseParams()));
        }
        // Advance one minute beyond the max session duration: next command will trigger
        // expiration and refresh
        clock.Tick(std::chrono::minutes(3));
        RequireInitialView();
        return;
      case State::ExplicitlyStopped:
        // Advance past the start of the session, then dispatch StopSession: on next
        // command, there will be no active session
        clock.Tick(std::chrono::minutes(5));
        scope.Process(RumCommand::StopSession(GetBaseParams()));
        RequireNoActiveSession();
        return;
    }
  }

  /**
   * Asserts that we have no active session.
   */
  void RequireNoActiveSession() const {
    // Our RumApplicationScope should have no active session
    const auto session_opt = scope.GetActiveSession();
    REQUIRE(!session_opt);
  }

  /**
   * Asserts that we still have our initial session and view, unchanged.
   */
  void RequireInitialView() const {
    // Our RumApplicationScope should have an active session
    const auto session_opt = scope.GetActiveSession();
    REQUIRE(session_opt);
    const RumSessionScope& session = *session_opt;

    // And it should be the session we started with
    REQUIRE(session.GetSessionID() == initial_session_id);

    // And the view we started with should still be active
    const auto view_opt = session.GetActiveView();
    REQUIRE(view_opt);
    const RumViewScope& view = *view_opt;
    REQUIRE(view.GetViewID() == initial_view_id);
    REQUIRE(view.GetKey() == initial_view_key);
    REQUIRE(view.GetName() == initial_view_name);
    const bool started_at_is_same = view.GetStartedAt() == initial_view_started_at;
    REQUIRE(started_at_is_same);
  }

  /**
   * Asserts that a new session was created, but no view is currently active within that
   * session.
   */
  void RequireNewSessionWithNoActiveView() const {
    // Our RumApplicationScope should have an active session
    const auto session_opt = scope.GetActiveSession();
    REQUIRE(session_opt);
    const RumSessionScope& session = *session_opt;

    // But it should be different than the session we started with
    REQUIRE(session.GetSessionID() != initial_session_id);

    // And our new session should have no active view
    const auto view_opt = session.GetActiveView();
    REQUIRE(!view_opt);
  }

  /**
   * Asserts that we have a new session with a new view whose basic details match the
   * original session.
   */
  void RequireRecreatedView() const {
    // Our RumApplicationScope should have an active session
    const auto session_opt = scope.GetActiveSession();
    REQUIRE(session_opt);
    const RumSessionScope& session = *session_opt;

    // But it should be different than the session we started with
    REQUIRE(session.GetSessionID() != initial_session_id);

    // And our new session should have an active view
    const auto view_opt = session.GetActiveView();
    REQUIRE(view_opt);
    const RumViewScope& view = *view_opt;

    // And it should have the same essential details as our original view
    REQUIRE(view.GetKey() == initial_view_key);
    REQUIRE(view.GetName() == initial_view_name);

    // But it should be an entirely new view scope
    REQUIRE(view.GetViewID() != initial_view_id);
    const bool started_later = view.GetStartedAt() > initial_view_started_at;
    REQUIRE(started_later);
  }

  /**
   * Asserts that we have a new session with an entirely different active view.
   */
  void RequireDifferentView(std::string_view key, std::string_view name) const {
    // Our RumApplicationScope should have an active session
    const auto session_opt = scope.GetActiveSession();
    REQUIRE(session_opt);
    const RumSessionScope& session = *session_opt;

    // But it should be different than the session we started with
    REQUIRE(session.GetSessionID() != initial_session_id);

    // And our new session should have an active view that's entirely different than the
    // view we started with, and that matches the given parameters
    const auto view_opt = session.GetActiveView();
    REQUIRE(view_opt);
    const RumViewScope& view = *view_opt;
    REQUIRE(view.GetViewID() != initial_view_id);
    REQUIRE(view.GetKey() == key);
    REQUIRE(view.GetName() == name);
    const bool started_later = view.GetStartedAt() > initial_view_started_at;
    REQUIRE(started_later);
  }

  RumCommandParams GetBaseParams() { return RumCommandParams(clock.Now(), {}, {}); }
};

TEST_CASE_METHOD(
    ViewTransferFixture,
    "RumApplicationScope::Process {on session refresh}",
    "[unit][rum]"
) {
  SECTION("{StopSession}") {
    SECTION("M not create a new session") {
      // Given any state in which the next command can't be handled by an active session
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration,
          ViewTransferFixture::State::ExplicitlyStopped
      ));

      // When we process a StopSession call in that expired or closed state
      scope.Process(RumCommand::StopSession(GetBaseParams()));

      // Then we are left without an active session, because StopSession was called
      RequireNoActiveSession();

      // But the previous session's end reason still reflects the actual reason, with
      // expiration taking precedence over explicit stoppage
      const auto end_reason = scope.GetMostRecentSession()->get().GetEndReason();
      REQUIRE(end_reason);
      switch (state) {
        case ViewTransferFixture::State::Initial:
          REQUIRE(false);
          break;
        case ViewTransferFixture::State::ExpiredDueToInactivity:
          REQUIRE(*end_reason == RumSessionScope::EndReason::TimedOutDueToInactivity);
          break;
        case ViewTransferFixture::State::ExpiredDueToMaxDuration:
          REQUIRE(*end_reason == RumSessionScope::EndReason::ExceededMaxDuration);
          break;
        case ViewTransferFixture::State::ExplicitlyStopped:
          REQUIRE(*end_reason == RumSessionScope::EndReason::Stopped);
          break;
      }
    }
  }

  SECTION("{StartView}") {
    SECTION("M create new view") {
      // Given any state in which the next command can't be handled by an active session
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration,
          ViewTransferFixture::State::ExplicitlyStopped
      ));

      // When we trigger session refresh with a StartView command
      scope.Process(RumCommand::StartView(GetBaseParams(), "bar", "Bar"));

      // Then a new session is created, and it contains our new view, not a copy of the
      // one we started with
      RequireDifferentView("bar", "Bar");
    }
  }

  SECTION("{StopView}") {
    SECTION("M create empty session W active session is expired") {
      // Given either state in which the active session is expired
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration
      ));

      // And a StopView command that targets either the current view or a different view
      auto view_key = GENERATE("foo", "bar");

      // When we trigger session refresh with that StopView command
      scope.Process(RumCommand::StopView(GetBaseParams(), view_key));

      // Then the session is refreshed, but it remains without an active view
      RequireNewSessionWithNoActiveView();
    }

    SECTION("M do nothing W no active session") {
      // Given a state in which there is no active session
      EnterState(ViewTransferFixture::State::ExplicitlyStopped);

      // When we process StopView after the session has been explicitly stopped
      scope.Process(RumCommand::StopView(GetBaseParams(), "foo"));

      // Then the command is ignored and no session refresh occurs
      RequireNoActiveSession();
    }
  }

  SECTION("{StartResource}") {
    SECTION("M create empty session W active session is expired") {
      // Given either state in which the active session is expired
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration
      ));

      // When we trigger session refresh with StartResource
      scope.Process(RumCommand::StartResource(GetBaseParams()));

      // Then the session is refreshed, and the last active view is recreated in order
      // to track our resource
      RequireRecreatedView();

      // TODO(RUM-12202): Verify that the active view contains an open resource scope?
    }

    SECTION("M do nothing W no active session") {
      // Given a state in which there is no active session
      EnterState(ViewTransferFixture::State::ExplicitlyStopped);

      // When we process StartResource after the session has been explicitly stopped
      scope.Process(RumCommand::StartResource(GetBaseParams()));

      // Then the StartResource call is ignored
      RequireNoActiveSession();
    }
  }

  SECTION("{StopResource}") {
    SECTION("M create empty session W active session is expired") {
      // Given either state in which the active session is expired
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration
      ));

      // When we trigger session refresh with StopResource
      scope.Process(RumCommand::StopResource(GetBaseParams()));

      // Then the session is refreshed, but it remains without an active view
      RequireNewSessionWithNoActiveView();
    }

    SECTION("M do nothing W no active session") {
      // Given a state in which there is no active session
      EnterState(ViewTransferFixture::State::ExplicitlyStopped);

      // When we process StopResource after the session has been explicitly stopped
      scope.Process(RumCommand::StopResource(GetBaseParams()));

      // Then the command is ignored and no session refresh occurs
      RequireNoActiveSession();
    }
  }

  SECTION("{AddAction}") {
    SECTION("M recreate last active view") {
      // Given any state in which the next command can't be handled by an active session
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration,
          ViewTransferFixture::State::ExplicitlyStopped
      ));

      // When we trigger session refresh with a AddAction command
      scope.Process(RumCommand::AddAction(GetBaseParams()));

      // Then the session is refreshed and the last active view is recreated
      RequireRecreatedView();
    }
  }

  SECTION("{StartAction}") {
    SECTION("M recreate last active view") {
      // Given any state in which the next command can't be handled by an active session
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration,
          ViewTransferFixture::State::ExplicitlyStopped
      ));

      // When we trigger session refresh with a StartAction command
      scope.Process(RumCommand::StartAction(GetBaseParams()));

      // Then the session is refreshed and the last active view is recreated
      RequireRecreatedView();
    }
  }

  SECTION("{StopAction}") {
    SECTION("M create empty session W active session is expired") {
      // Given either state in which the active session is expired
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration
      ));

      // When we trigger session refresh with StopAction
      scope.Process(RumCommand::StopAction(GetBaseParams()));

      // Then the session is refreshed, but it remains without an active view
      RequireNewSessionWithNoActiveView();
    }

    SECTION("M do nothing W no active session") {
      // Given a state in which there is no active session
      EnterState(ViewTransferFixture::State::ExplicitlyStopped);

      // When we process StopAction after the session has been explicitly stopped
      scope.Process(RumCommand::StopAction(GetBaseParams()));

      // Then the command is ignored and no session refresh occurs
      RequireNoActiveSession();
    }
  }

  SECTION("{StartResource + AddAction}") {
    SECTION("M recreate last active view") {
      // Given any state in which the next command can't be handled by an active session
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration,
          ViewTransferFixture::State::ExplicitlyStopped
      ));

      // When we trigger session refresh with a StartResource command
      scope.Process(RumCommand::StartResource(GetBaseParams()));

      // And then we handle an AddAction command post-refresh
      clock.Tick(std::chrono::seconds(1));
      scope.Process(RumCommand::AddAction(GetBaseParams()));

      // Then the session is refreshed and the last active view is recreated
      RequireRecreatedView();
    }
  }

  SECTION("{StopResource + AddAction}") {
    SECTION("M recreate last active view") {
      // Given any state in which the next command can't be handled by an active session
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration,
          ViewTransferFixture::State::ExplicitlyStopped
      ));

      // When we trigger session refresh with a StopResource command
      scope.Process(RumCommand::StopResource(GetBaseParams()));

      // And then we handle an AddAction command post-refresh
      clock.Tick(std::chrono::seconds(1));
      scope.Process(RumCommand::AddAction(GetBaseParams()));

      // Then the session is refreshed and the last active view is recreated
      RequireRecreatedView();
    }
  }

  SECTION("{StopView + AddAction}") {
    SECTION("M recreate last active view W StopView does not target active view") {
      // Given any state in which the next command can't be handled by an active session
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration,
          ViewTransferFixture::State::ExplicitlyStopped
      ));

      // When we trigger session refresh with a StopView command that targets a
      // different view than the one that's active
      scope.Process(RumCommand::StopView(GetBaseParams(), "bar"));

      // And then we handle an AddAction command post-refresh
      clock.Tick(std::chrono::seconds(1));
      scope.Process(RumCommand::AddAction(GetBaseParams()));

      // Then the session is refreshed and the last active view is recreated
      RequireRecreatedView();
    }

    SECTION(
        "M create empty session W active session expired and StopView targets active "
        "view"
    ) {
      // Given either state in which the active session is expired
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration
      ));

      // When we trigger session refresh with a StopView command that targets the active
      // view
      scope.Process(RumCommand::StopView(GetBaseParams(), "foo"));

      // And then we handle an AddAction command post-refresh
      clock.Tick(std::chrono::seconds(1));
      scope.Process(RumCommand::AddAction(GetBaseParams()));

      // TODO(RUM-12247): With background tracking enabled, the new session would have
      // a 'Background' view

      // Then the session is refreshed, but it has no active view, because carrying over
      // the view would violate the intention of the StopView command
      RequireNewSessionWithNoActiveView();
    }

    SECTION(
        "M create empty session W no active session and StopView targets active view"
    ) {
      // Given a state in which there is no active session
      EnterState(ViewTransferFixture::State::ExplicitlyStopped);

      // When we trigger session refresh with a StopView command that targets the active
      // view (which should clear any cached view-transfer state for that view)
      scope.Process(RumCommand::StopView(GetBaseParams(), "foo"));

      // Then we still have no active session, because StopView does not trigger session
      // refresh after explicit stop
      RequireNoActiveSession();

      // Next: When we handle an AddAction command post-refresh
      clock.Tick(std::chrono::seconds(1));
      scope.Process(RumCommand::AddAction(GetBaseParams()));

      // TODO(RUM-12247): With background tracking enabled, the new session would have
      // a 'Background' view

      // Then the session is refreshed, but it has no active view, because carrying over
      // the view would violate the intention of the earlier StopView command
      RequireNewSessionWithNoActiveView();
    }
  }

  SECTION("{StopResource + StartView + StartAction}") {
    SECTION("M create new view") {
      // Given any state in which the next command can't be handled by an active session
      EnterState(GENERATE(
          ViewTransferFixture::State::ExpiredDueToInactivity,
          ViewTransferFixture::State::ExpiredDueToMaxDuration,
          ViewTransferFixture::State::ExplicitlyStopped
      ));

      // When we trigger session refresh with a StopResource command
      scope.Process(RumCommand::StopResource(GetBaseParams()));

      // Then either we end up with an expired session or we remain in our
      // post-StopSession state with no active session
      if (state != ViewTransferFixture::State::ExplicitlyStopped) {
        RequireNewSessionWithNoActiveView();
      } else {
        RequireNoActiveSession();
      }

      // Next: When we process StartView to explicitly register a new view
      scope.Process(RumCommand::StartView(GetBaseParams(), "bar", "Bar"));

      // Then that view is active in a new session
      RequireDifferentView("bar", "Bar");

      // Next: When we process StartAction
      scope.Process(RumCommand::StartAction(GetBaseParams()));

      // Then we remain in our newly-created view: the last-active view from our
      // original session is not recreated, since the new view supersedes it
      RequireDifferentView("bar", "Bar");

      // TODO(RUM-11369): Verify that action exists in active view scope?
    }
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
