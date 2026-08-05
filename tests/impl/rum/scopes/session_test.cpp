// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scopes/session.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/rum/context.hpp"
#include "datadog/impl/rum/scopes/application.hpp"
#include "datadog/impl/rum/scopes/view.hpp"

#include "mock/clock.hpp"
#include "support/context.hpp"
#include "support/rum_event_capture.hpp"

using namespace datadog;
using namespace datadog::impl;

class SessionFixture {
 protected:
  static constexpr const char* APPLICATION_ID = "a991ca10-4004-4004-4004-beefbeefbeef";
  static constexpr const char* SESSION_ID = "5e551017-4114-4114-4114-beeeefbeeeef";

  MockClock clock;

  RumConfig config;
  RumScopeDependencies deps;
  RumApplicationScope parent;
  RumSessionScope scope;

  CoreContext context;
  EventWriter writer;

 public:
  SessionFixture(bool is_session_sampled = true)
      : config(APPLICATION_ID),
        deps(config, clock),
        parent(deps),
        scope(
            deps,
            parent,
            true,
            is_session_sampled,
            *UUID::Parse(SESSION_ID),
            RumSessionPrecondition::UserAppLaunch,
            Timestamp{std::chrono::duration_cast<Duration>(
                std::chrono::milliseconds{1700000000000}
            )},
            std::nullopt
        ),
        context(MOCK_CONTEXT),
        writer([](Block, Block, bool) { return true; }) {
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
    RumScopeResult result =
        scope.Process(RumCommand::StopAction(GetBaseParams(), "foo"), context, writer);

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
    RumScopeResult result =
        scope.Process(RumCommand::StopAction(GetBaseParams(), "foo"), context, writer);

    // Then the scope is still open, as 7m does not exceed our inactivity timeout
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // Next: When we process any user interaction 14 minutes thereafter
    clock.Tick(std::chrono::minutes(14));
    result =
        scope.Process(RumCommand::StopAction(GetBaseParams(), "foo"), context, writer);

    // Then the result is the same, as 14m does not exceed our timeout either, and our
    // previous command refreshed the last-interaction timestamp
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // Next: When we wait a full 16 minutes before processing the next user interaction
    clock.Tick(std::chrono::minutes(16));
    result =
        scope.Process(RumCommand::StopAction(GetBaseParams(), "foo"), context, writer);

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
      const auto result = scope.Process(
          RumCommand::StopAction(GetBaseParams(), "foo"), context, writer
      );
      REQUIRE(result == RumScopeResult::RemainOpen);
      REQUIRE(scope.GetEndReason() == std::nullopt);
    }

    // Next: When we advance time to T+4h01m and process another user interaction
    clock.Tick(std::chrono::minutes(11));
    const auto result =
        scope.Process(RumCommand::StopAction(GetBaseParams(), "foo"), context, writer);

    // Then our session scope is closed and the command is not handled
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(scope.GetEndReason() == RumSessionScope::EndReason::ExceededMaxDuration);
  }

  SECTION("M close with Stopped W StopSession is processed") {
    // Given a session scope that is active before StopSession is processed
    REQUIRE(scope.IsActive());

    // When we process StopSession
    const auto result =
        scope.Process(RumCommand::StopSession(GetBaseParams()), context, writer);

    // Then the scope is explicitly closed and no longer reports itself as active
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(scope.GetEndReason() == RumSessionScope::EndReason::Stopped);
    REQUIRE_FALSE(scope.IsActive());
  }

  SECTION(
      "M close with TimedOutDueToInactivity W StopSession is processed after inactivity"
  ) {
    // When we process StopSession after 15m+ of inactivity
    clock.Tick(std::chrono::minutes(16));
    const auto result =
        scope.Process(RumCommand::StopSession(GetBaseParams()), context, writer);

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
      const auto result = scope.Process(
          RumCommand::StopAction(GetBaseParams(), "foo"), context, writer
      );
      REQUIRE(result == RumScopeResult::RemainOpen);
      REQUIRE(scope.GetEndReason() == std::nullopt);
    }
    clock.Tick(std::chrono::minutes(11));
    const auto result =
        scope.Process(RumCommand::StopSession(GetBaseParams()), context, writer);

    // Then the scope is closed, but the excessive duration takes precendence over the
    // explicit stop call
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(scope.GetEndReason() == RumSessionScope::EndReason::ExceededMaxDuration);
  }

  SECTION("M start new view W StartView is processed") {
    // Given a session with no views
    REQUIRE(scope.GetActiveView() == std::nullopt);

    // When we process StartView
    const auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "view-a", "View A"), context, writer
    );

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
    scope.Process(
        RumCommand::StartView(GetBaseParams(), "view-a", "View A"), context, writer
    );
    REQUIRE(scope.GetActiveView()->get().GetKey() == "view-a");
    const UUID view_a_id = scope.GetActiveView()->get().GetViewID();

    // When we create another view B in response to a StartView command
    const auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "view-b", "View B"), context, writer
    );

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
    scope.Process(
        RumCommand::StartView(GetBaseParams(), "view-a", "View A"), context, writer
    );
    REQUIRE(scope.GetActiveView()->get().GetKey() == "view-a");
    const UUID initial_view_id = scope.GetActiveView()->get().GetViewID();

    // When we create another view, also 'view-a', in response to a StartView command
    const auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "view-a", "View A"), context, writer
    );

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
    scope.Process(
        RumCommand::StartView(GetBaseParams(), "view-a", "View A"), context, writer
    );
    REQUIRE(scope.GetActiveView()->get().GetKey() == "view-a");

    // When we process a StopView command that targets 'view-a'
    const auto result =
        scope.Process(RumCommand::StopView(GetBaseParams(), "view-a"), context, writer);

    // Then our session remains open, but it no longer has an active view
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetActiveView() == std::nullopt);
  }

  SECTION("M do nothing W StopView does not target any existing view") {
    // Given a session with 'view-a' active
    scope.Process(
        RumCommand::StartView(GetBaseParams(), "view-a", "View A"), context, writer
    );
    REQUIRE(scope.GetActiveView()->get().GetKey() == "view-a");
    const UUID initial_view_id = scope.GetActiveView()->get().GetViewID();

    // When we process a StopView command that targets 'view-b'
    const auto result =
        scope.Process(RumCommand::StopView(GetBaseParams(), "view-b"), context, writer);

    // Then our original view scope remains in place: nothing changes
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetActiveView().has_value());
    REQUIRE(scope.GetActiveView()->get().GetViewID() == initial_view_id);
  }

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
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "foo", ""), context, writer
    );

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
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "foo", ""), context, writer
    );
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);
    clock.Tick(std::chrono::minutes(10));
    result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "foo", ""), context, writer
    );
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(scope.GetEndReason() == std::nullopt);

    // Next: When we wait 16 minutes and process another StartView
    clock.Tick(std::chrono::minutes(16));
    result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "foo", ""), context, writer
    );

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
      auto result = scope.Process(
          RumCommand::StartView(GetBaseParams(), "foo", ""), context, writer
      );
      REQUIRE(result == RumScopeResult::RemainOpen);
      REQUIRE(scope.GetEndReason() == std::nullopt);
    }

    // Next: When we advance time to T+4h01m and process another user interaction
    clock.Tick(std::chrono::minutes(11));
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "foo", ""), context, writer
    );

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
    auto result =
        scope.Process(RumCommand::StopSession(GetBaseParams()), context, writer);

    // Then the session is closed as usual: the command is heeded even though the
    // session isn't sampled
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(scope.GetEndReason().value() == RumSessionScope::EndReason::Stopped);
  }
}

/**
 * Fixture for session tests that need to capture emitted events.
 */
class SessionEventFixture {
 protected:
  static constexpr const char* APPLICATION_ID = "a991ca10-4004-4004-4004-beefbeefbeef";
  static constexpr const char* SESSION_ID = "5e551017-4114-4114-4114-beeeefbeeeef";
  static constexpr const char* VIEW_ID = "141ee144-4224-4224-4224-beeeeeeeeeef";

  MockClock clock;

  RumConfig config;
  RumScopeDependencies deps;
  RumApplicationScope parent;
  RumSessionScope scope;

  // Event capture - delegates to shared RumEventCapture harness
  RumEventCapture event_capture;

 public:
  CoreContext GetTestContext() { return event_capture.GetContext(); }
  EventWriter GetTestWriter() { return event_capture.GetWriter(); }
  SessionEventFixture()
      : config(APPLICATION_ID),
        deps(config, clock),
        parent(deps),
        scope(
            deps,
            parent,
            true,
            true,
            *UUID::Parse(SESSION_ID),
            RumSessionPrecondition::UserAppLaunch,
            Timestamp{std::chrono::duration_cast<Duration>(
                std::chrono::milliseconds{1700000000000}
            )},
            std::nullopt
        ),
        event_capture(APPLICATION_ID, SESSION_ID, nullptr) {
    deps.diagnostic_logger = event_capture.GetFeatureScope().diagnostic_logger;
    clock.FreezeAtMilliseconds(1700000000000);
  }

  RumCommandParams GetBaseParams(const Attribute& attrs = Attribute()) {
    return RumCommandParams(clock.Now(), {}, attrs);
  }

  void StartView(
      std::string_view key = "my-view-key", std::string_view name = "My View"
  ) {
    scope.Process(
        RumCommand::StartView(GetBaseParams(), key, name),
        GetTestContext(),
        GetTestWriter()
    );
  }
};

TEST_CASE_METHOD(
    SessionEventFixture, "RumSessionScope ReportAppDisplayInitialized", "[unit][rum]"
) {
  SECTION("M emit TTID vital event W called once with an active view") {
    // Given an active session with a view, and a known process launch time
    StartView();

    // Build a context with a specific process launch time so the duration is
    // deterministic: launch at t=0, current time at t=5s → 5e9 ns
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;
    // clock is frozen at 1700000000000 ms, so duration = 5000ms = 5e9 ns

    // When we process a ReportAppDisplayInitialized command
    const auto result = scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );

    // Then the session remains open
    REQUIRE(result == RumScopeResult::RemainOpen);

    // And a single vital event is emitted
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    const auto& ev = vitals[0];

    // With the correct type fields
    REQUIRE(ev["type"] == "vital");
    REQUIRE(ev["vital"]["type"] == "app_launch");
    REQUIRE(ev["vital"]["app_launch_metric"] == "ttid");
    REQUIRE(ev["vital"]["name"] == "time_to_initial_display");

    // With the correct duration (5000 ms = 5e9 ns)
    REQUIRE(ev["vital"]["duration"].get<double>() == 5000000000.0);

    // With a valid, non-zero vital ID
    const std::string vital_id_str = ev["vital"]["id"];
    const auto vital_id = UUID::Parse(vital_id_str);
    REQUIRE(vital_id.has_value());
    REQUIRE(*vital_id != UUID::Zero);

    // date is the process launch time (ms since epoch), not the time of the call
    REQUIRE(ev["date"].get<int64_t>() == 1699999995000LL);

    // With the view URL from the active view
    REQUIRE(ev["view"]["url"] == "my-view-key");
  }

  SECTION(
      "M emit TTID vital event with zero view fields W called with no active view"
  ) {
    // Given an active session with NO active view

    // When we process a ReportAppDisplayInitialized command
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    const auto result = scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );

    // Then the session remains open
    REQUIRE(result == RumScopeResult::RemainOpen);

    // And a vital event is emitted (matching iOS/Android behaviour)
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    const auto& ev = vitals[0];

    REQUIRE(ev["type"] == "vital");
    REQUIRE(ev["vital"]["app_launch_metric"] == "ttid");
    REQUIRE(ev["vital"]["duration"].get<double>() == 5000000000.0);

    // With zero view ID and empty URL (matching iOS NULL_UUID / Android
    // EMPTY_RUM_SESSION_ID)
    REQUIRE(ev["view"]["id"] == "00000000-0000-0000-0000-000000000000");
    REQUIRE(ev["view"]["url"] == "");

    // And no warning is emitted
    REQUIRE(event_capture.Diagnostics().warning.empty());
  }

  SECTION(
      "M emit no vital event and warn W duration is zero (launch time equals now)"
  ) {
    // Given an active session with a view, and process_launch_time == now
    StartView();
    CoreContext ctx = GetTestContext();
    // Frozen clock is at 1700000000000 ms; setting launch == now → duration_ns == 0
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1700000000000})
    };

    // When we process a ReportAppDisplayInitialized command
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );

    // Then no vital event is emitted
    REQUIRE(event_capture.Vitals().empty());

    // And a warning was emitted
    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("outside expected range") != std::string::npos);
  }

  SECTION("M emit no vital event and warn W clock skew produces negative duration") {
    // Given an active session with a view, but clock skew makes now < launch
    StartView();
    // Set launch time to 10s after the frozen clock (1700000000000 ms)
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1700000010000})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;

    // When we process a ReportAppDisplayInitialized command
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );

    // Then no vital event is emitted
    REQUIRE(event_capture.Vitals().empty());

    // And a warning was emitted
    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("outside expected range") != std::string::npos);
  }

  SECTION("M emit no vital event and warn W duration is at or above 60s") {
    // Given an active session with a view, and a launch time exactly 60s before now
    StartView();
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999940000})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;
    // duration = 60000ms = 60e9 ns — exactly at the upper bound (exclusive)

    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );

    REQUIRE(event_capture.Vitals().empty());

    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("outside expected range") != std::string::npos);
  }

  SECTION("M emit vital event W duration is just inside valid range (1 ns)") {
    // Given a launch time 1 ns before now
    // Frozen clock is at 1700000000000 ms = 1700000000000000000 ns.
    // We need launch such that (now - launch) == 1 ns.
    // Easiest: set launch to (now - 1ns). The frozen clock is in milliseconds,
    // so use a launch time 1ms before now (1 ms = 1e6 ns, well inside range).
    StartView();
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999999999})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;
    // duration = 1ms = 1e6 ns — inside (0, 60e9)

    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    REQUIRE(vitals[0]["vital"]["duration"].get<double>() == 1000000.0);
    REQUIRE(event_capture.Diagnostics().warning.empty());
  }

  SECTION("M emit vital event W duration is just below 60s upper bound") {
    // Launch time 59999ms before frozen clock → duration = 59999ms = 59.999e9 ns
    StartView();
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999940001})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;

    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    REQUIRE(vitals[0]["vital"]["duration"].get<double>() == 59999000000.0);
    REQUIRE(event_capture.Diagnostics().warning.empty());
  }
}

TEST_CASE_METHOD(
    SessionEventFixture, "RumSessionScope ReportAppFullyDisplayed", "[unit][rum]"
) {
  SECTION("M emit TTFD vital event W called once with an active view") {
    // Given an active session with a view, and a known process launch time
    StartView();

    // Build a context with a specific process launch time so the duration is
    // deterministic: launch at t=0, current time at t=5s → 5e9 ns
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;
    // clock is frozen at 1700000000000 ms, so duration = 5000ms = 5e9 ns

    // TTID must fire before TTFD for the immediate-emit path
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );

    // When we process a ReportAppFullyDisplayed command
    const auto result = scope.Process(
        RumCommand::ReportAppFullyDisplayed(GetBaseParams()), ctx, GetTestWriter()
    );

    // Then the session remains open
    REQUIRE(result == RumScopeResult::RemainOpen);

    // And two vital events are emitted (TTID + TTFD)
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    const auto& ev = vitals[1];

    // With the correct type fields
    REQUIRE(ev["type"] == "vital");
    REQUIRE(ev["vital"]["type"] == "app_launch");
    REQUIRE(ev["vital"]["app_launch_metric"] == "ttfd");
    REQUIRE(ev["vital"]["name"] == "time_to_full_display");

    // With the correct duration (5000 ms = 5e9 ns)
    REQUIRE(ev["vital"]["duration"].get<double>() == 5000000000.0);

    // With a valid, non-zero vital ID
    const std::string vital_id_str = ev["vital"]["id"];
    const auto vital_id = UUID::Parse(vital_id_str);
    REQUIRE(vital_id.has_value());
    REQUIRE(*vital_id != UUID::Zero);

    // date is the process launch time (ms since epoch), not the time of the call
    REQUIRE(ev["date"].get<int64_t>() == 1699999995000LL);

    // With the view URL from the active view
    REQUIRE(ev["view"]["url"] == "my-view-key");
  }

  SECTION(
      "M emit TTFD vital event with zero view fields W called with no active view"
  ) {
    // Given an active session with NO active view
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };

    // TTID must fire before TTFD for the immediate-emit path
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );

    // When we process a ReportAppFullyDisplayed command
    const auto result = scope.Process(
        RumCommand::ReportAppFullyDisplayed(GetBaseParams()), ctx, GetTestWriter()
    );

    // Then the session remains open
    REQUIRE(result == RumScopeResult::RemainOpen);

    // And two vital events are emitted: TTID + TTFD (matching iOS/Android behavior)
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    const auto& ev = vitals[1];

    REQUIRE(ev["type"] == "vital");
    REQUIRE(ev["vital"]["app_launch_metric"] == "ttfd");
    REQUIRE(ev["vital"]["duration"].get<double>() == 5000000000.0);

    // With zero view ID and empty URL
    REQUIRE(ev["view"]["id"] == "00000000-0000-0000-0000-000000000000");
    REQUIRE(ev["view"]["url"] == "");

    // And no warning is emitted
    REQUIRE(event_capture.Diagnostics().warning.empty());
  }

  SECTION(
      "M emit no vital event and warn W duration is zero (launch time equals now)"
  ) {
    // Given an active session with a view, and process_launch_time == now
    StartView();
    CoreContext ctx = GetTestContext();
    // Frozen clock is at 1700000000000 ms; setting launch == now → duration_ns == 0
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1700000000000})
    };

    // When we process a ReportAppFullyDisplayed command
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(GetBaseParams()), ctx, GetTestWriter()
    );

    // Then no vital event is emitted
    REQUIRE(event_capture.Vitals().empty());

    // And a warning was emitted
    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("outside expected range") != std::string::npos);
  }

  SECTION("M emit no vital event and warn W clock skew produces negative duration") {
    // Given an active session with a view, but clock skew makes now < launch
    StartView();
    // Set launch time to 10s after the frozen clock (1700000000000 ms)
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1700000010000})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;

    // When we process a ReportAppFullyDisplayed command
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(GetBaseParams()), ctx, GetTestWriter()
    );

    // Then no vital event is emitted
    REQUIRE(event_capture.Vitals().empty());

    // And a warning was emitted
    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("outside expected range") != std::string::npos);
  }

  SECTION("M emit no vital event and warn W duration is at or above 90s") {
    // Given an active session with a view, and a launch time exactly 90s before now
    StartView();
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999910000})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;
    // duration = 90000ms = 90e9 ns - exactly at the upper bound (exclusive)

    scope.Process(
        RumCommand::ReportAppFullyDisplayed(GetBaseParams()), ctx, GetTestWriter()
    );

    REQUIRE(event_capture.Vitals().empty());

    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("outside expected range") != std::string::npos);
  }

  SECTION("M emit vital event W duration is just inside valid range (1 ms)") {
    // Given a launch time 1ms before the frozen clock → duration = 1ms = 1e6 ns
    StartView();
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999999999})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;

    // TTID must fire before TTFD for the immediate-emit path
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()), ctx, GetTestWriter()
    );
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(GetBaseParams()), ctx, GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    REQUIRE(vitals[1]["vital"]["duration"].get<double>() == 1000000.0);
    REQUIRE(event_capture.Diagnostics().warning.empty());
  }

  SECTION("M emit vital event W duration is just below 90s upper bound") {
    // Launch time 89999ms before frozen clock → duration = 89999ms = 89.999e9 ns
    StartView();
    const Timestamp launch_time{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999910001})
    };
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = launch_time;

    // TTID must fire before TTFD for the immediate-emit path.
    // TTID duration here is 89999ms which is within TTID's 60s limit, so we
    // use a separate ctx with a closer launch time for TTID.
    CoreContext ttid_ctx = GetTestContext();
    ttid_ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()),
        ttid_ctx,
        GetTestWriter()
    );
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(GetBaseParams()), ctx, GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    REQUIRE(vitals[1]["vital"]["duration"].get<double>() == 89999000000.0);
    REQUIRE(event_capture.Diagnostics().warning.empty());
  }

  // --- TTID/TTFD immediate-path clamp tests ---
  // These sections exercise the case where TTID fires first and TTFD fires
  // second but with a raw duration smaller than TTID (the timestamp-capture race).

  SECTION(
      "M clamp TTFD to TTID W TTID fires first and raw TTFD duration < TTID duration"
  ) {
    // TTID duration: 5s (5e9 ns). TTFD raw duration: 3s (3e9 ns).
    // Expected: TTFD emitted with duration = max(3e9, 5e9) = 5e9 ns.
    StartView();
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999990000})
    };
    // clock frozen at 1700000000000 ms → duration from launch = 10s.
    // Override issued_at for each command to get the desired durations.
    const Timestamp ttid_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    RumCommandParams ttid_params(ttid_issued_at, {}, {});
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(std::move(ttid_params)),
        ctx,
        GetTestWriter()
    );

    const Timestamp ttfd_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999993000})
    };
    RumCommandParams ttfd_params(ttfd_issued_at, {}, {});
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(std::move(ttfd_params)),
        ctx,
        GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    REQUIRE(vitals[0]["vital"]["app_launch_metric"] == "ttid");
    REQUIRE(vitals[0]["vital"]["duration"].get<double>() == 5000000000.0);
    REQUIRE(vitals[1]["vital"]["app_launch_metric"] == "ttfd");
    REQUIRE(vitals[1]["vital"]["duration"].get<double>() == 5000000000.0);
    // No warning: TTFD after TTID is the normal in-order path
    REQUIRE(event_capture.Diagnostics().warning.empty());
  }

  SECTION("M not clamp TTFD W TTID fires first and raw TTFD duration > TTID duration") {
    // TTID duration: 5s (5e9 ns). TTFD raw duration: 8s (8e9 ns).
    // Expected: TTFD emitted with duration = max(8e9, 5e9) = 8e9 ns (no clamp).
    StartView();
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999990000})
    };
    const Timestamp ttid_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    RumCommandParams ttid_params(ttid_issued_at, {}, {});
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(std::move(ttid_params)),
        ctx,
        GetTestWriter()
    );

    const Timestamp ttfd_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999998000})
    };
    RumCommandParams ttfd_params(ttfd_issued_at, {}, {});
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(std::move(ttfd_params)),
        ctx,
        GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    REQUIRE(vitals[0]["vital"]["app_launch_metric"] == "ttid");
    REQUIRE(vitals[0]["vital"]["duration"].get<double>() == 5000000000.0);
    REQUIRE(vitals[1]["vital"]["app_launch_metric"] == "ttfd");
    REQUIRE(vitals[1]["vital"]["duration"].get<double>() == 8000000000.0);
    REQUIRE(event_capture.Diagnostics().warning.empty());
  }

  // --- TTID/TTFD ordering tests ---
  // These sections exercise the deferred-emit path: TTFD called before TTID.

  SECTION(
      "M defer TTFD and emit both events W TTFD is called before TTID "
      "(raw TTFD < TTID, duration clamped to TTID)"
  ) {
    // Given a view and a launch time of t=0 (1700000000000 ms - 10000 ms).
    // TTFD raw duration: 3s (3e9 ns). TTID duration: 5s (5e9 ns).
    // Expected: TTFD emitted with duration = max(3e9, 5e9) = 5e9 ns.
    StartView();
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999990000})
    };
    // clock is frozen at 1700000000000 ms, so duration from launch = 10s = 10e9 ns.
    // We want TTFD raw = 3s: rewind the command's issued_at by overriding the params.
    // Do this by temporarily advancing a separate params timestamp:
    // Use a params with issued_at = launch + 3s.
    const Timestamp ttfd_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999993000})
    };
    RumCommandParams ttfd_params(ttfd_issued_at, {}, {});

    // When TTFD is processed first (before TTID)
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(std::move(ttfd_params)),
        ctx,
        GetTestWriter()
    );

    // Then no event is emitted yet
    REQUIRE(event_capture.Vitals().empty());
    // And no warning yet (the warning fires at TTID time)
    REQUIRE(event_capture.Diagnostics().warning.empty());

    // When TTID is subsequently processed (issued_at = launch + 5s)
    const Timestamp ttid_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    RumCommandParams ttid_params(ttid_issued_at, {}, {});
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(std::move(ttid_params)),
        ctx,
        GetTestWriter()
    );

    // Then both events are emitted
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);

    // First event is TTID
    REQUIRE(vitals[0]["vital"]["app_launch_metric"] == "ttid");
    REQUIRE(vitals[0]["vital"]["duration"].get<double>() == 5000000000.0);

    // Second event is TTFD, clamped to TTID duration
    REQUIRE(vitals[1]["vital"]["app_launch_metric"] == "ttfd");
    REQUIRE(vitals[1]["vital"]["duration"].get<double>() == 5000000000.0);

    // And a warning was emitted at TTID time
    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(
        warnings[0].find("ReportAppFullyDisplayed was called before") !=
        std::string::npos
    );
  }

  SECTION(
      "M defer TTFD and emit both events W TTFD is called before TTID "
      "(raw TTFD > TTID, duration kept as raw TTFD)"
  ) {
    // TTFD raw = 8s, TTID = 5s. Expected: TTFD duration = max(8e9, 5e9) = 8e9 ns.
    StartView();
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999990000})
    };

    const Timestamp ttfd_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999998000})
    };
    RumCommandParams ttfd_params(ttfd_issued_at, {}, {});
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(std::move(ttfd_params)),
        ctx,
        GetTestWriter()
    );
    REQUIRE(event_capture.Vitals().empty());

    const Timestamp ttid_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    RumCommandParams ttid_params(ttid_issued_at, {}, {});
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(std::move(ttid_params)),
        ctx,
        GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    REQUIRE(vitals[0]["vital"]["app_launch_metric"] == "ttid");
    REQUIRE(vitals[0]["vital"]["duration"].get<double>() == 5000000000.0);
    REQUIRE(vitals[1]["vital"]["app_launch_metric"] == "ttfd");
    REQUIRE(vitals[1]["vital"]["duration"].get<double>() == 8000000000.0);

    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(
        warnings[0].find("ReportAppFullyDisplayed was called before") !=
        std::string::npos
    );
  }

  SECTION(
      "M use TTFD-call-time attribute snapshot W TTFD is deferred until TTID fires"
  ) {
    // Given a view and a known process launch time
    StartView();
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999990000})
    };

    // When ReportAppFullyDisplayed is called with a global attribute "env" = "prod"
    Attribute ttfd_global = Attribute::Object();
    ttfd_global.SetObjectProperty("env", Attribute::String("prod"));
    const Timestamp ttfd_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999993000})
    };
    RumCommandParams ttfd_params(ttfd_issued_at, ttfd_global, {});
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(std::move(ttfd_params)),
        ctx,
        GetTestWriter()
    );
    REQUIRE(event_capture.Vitals().empty());

    // And then ReportAppDisplayInitialized is called with a different global attribute
    // "env" = "staging" (simulating a global attribute change between the two calls)
    Attribute ttid_global = Attribute::Object();
    ttid_global.SetObjectProperty("env", Attribute::String("staging"));
    const Timestamp ttid_issued_at{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    RumCommandParams ttid_params(ttid_issued_at, ttid_global, {});
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(std::move(ttid_params)),
        ctx,
        GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);

    // The TTID event carries the TTID-call-time attribute snapshot
    REQUIRE(vitals[0]["vital"]["app_launch_metric"] == "ttid");
    REQUIRE(vitals[0]["context"]["env"] == "staging");

    // The deferred TTFD event carries the TTFD-call-time attribute snapshot,
    // not the TTID-call-time snapshot
    REQUIRE(vitals[1]["vital"]["app_launch_metric"] == "ttfd");
    REQUIRE(vitals[1]["context"]["env"] == "prod");
  }

  SECTION(
      "M drop TTFD and warn W raw TTFD duration is out of range before TTID fires"
  ) {
    // Given a launch time such that TTFD raw duration = 95s (exceeds 90s max).
    StartView();
    CoreContext ctx = GetTestContext();
    ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999905000})
    };
    // clock frozen at 1700000000000 ms → raw TTFD duration = 95s = 95e9 ns (out of
    // range)

    scope.Process(
        RumCommand::ReportAppFullyDisplayed(GetBaseParams()), ctx, GetTestWriter()
    );

    // Then no event is emitted and no pending TTFD is stored
    REQUIRE(event_capture.Vitals().empty());

    // A range warning was emitted immediately
    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("outside expected range") != std::string::npos);

    // When TTID subsequently fires with a valid duration, only TTID is emitted
    // (no deferred TTFD, since the TTFD was rejected by the range check above)
    CoreContext ttid_ctx = GetTestContext();
    ttid_ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()),
        ttid_ctx,
        GetTestWriter()
    );
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    REQUIRE(vitals[0]["vital"]["app_launch_metric"] == "ttid");
  }

  SECTION("M not emit TTFD W TTFD is pending but TTID subsequently fails range check") {
    // Given a valid TTFD raw duration (5s) stored as pending,
    // but TTID is called with a launch time that makes TTID duration out of range.
    StartView();

    // TTFD: valid 5s duration relative to a launch time 5s before frozen clock
    CoreContext ttfd_ctx = GetTestContext();
    ttfd_ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1699999995000})
    };
    scope.Process(
        RumCommand::ReportAppFullyDisplayed(GetBaseParams()), ttfd_ctx, GetTestWriter()
    );
    REQUIRE(event_capture.Vitals().empty());
    REQUIRE(event_capture.Diagnostics().warning.empty());

    // TTID: launch time equals now → duration = 0 (fails range check)
    CoreContext ttid_ctx = GetTestContext();
    ttid_ctx.process_launch_time = Timestamp{
        std::chrono::duration_cast<Duration>(std::chrono::milliseconds{1700000000000})
    };
    scope.Process(
        RumCommand::ReportAppDisplayInitialized(GetBaseParams()),
        ttid_ctx,
        GetTestWriter()
    );

    // Then no events at all: TTID was rejected and _ttid_has_fired was not set,
    // so the pending TTFD is never consumed.
    REQUIRE(event_capture.Vitals().empty());

    // A warning about the out-of-range TTID was emitted, but no TTFD warning
    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("outside expected range") != std::string::npos);
  }
}

TEST_CASE_METHOD(SessionEventFixture, "RumSessionScope operations", "[unit][rum]") {
  SECTION("M emit start vital event W StartOperation is processed") {
    // Given an active session with a view
    StartView();

    // When we process a StartOperation command
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // Then a vital event is emitted
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    const auto& ev = vitals[0];
    REQUIRE(ev["type"] == "vital");
    REQUIRE(ev["vital"]["name"] == "checkout");
    REQUIRE(ev["vital"]["type"] == "operation_step");
    REQUIRE(ev["vital"]["step_type"] == "start");
    // Vital ID is a valid, nonzero UUID
    const std::string vital_id_str = ev["vital"]["id"];
    const auto vital_id = UUID::Parse(vital_id_str);
    REQUIRE(vital_id.has_value());
    REQUIRE(*vital_id != UUID::Zero);
    // No operation_key or failure_reason
    REQUIRE(ev["vital"].count("operation_key") == 0);
    REQUIRE(ev["vital"].count("failure_reason") == 0);
  }

  SECTION("M emit end vital event with no failure W SucceedOperation is processed") {
    // Given an active session with a view and an active operation
    StartView();
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // When we stop the operation successfully
    scope.Process(
        RumCommand::StopOperation(
            GetBaseParams(), "checkout", std::nullopt, std::nullopt
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // Then two vital events are emitted (start + end)
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    const auto& start_ev = vitals[0];
    const auto& end_ev = vitals[1];
    REQUIRE(end_ev["vital"]["step_type"] == "end");
    // Vital ID is a valid, nonzero UUID
    const std::string end_vital_id_str = end_ev["vital"]["id"];
    const auto end_vital_id = UUID::Parse(end_vital_id_str);
    REQUIRE(end_vital_id.has_value());
    REQUIRE(*end_vital_id != UUID::Zero);
    // End vital ID is distinct from start vital ID
    const std::string start_vital_id_str = start_ev["vital"]["id"];
    const auto start_vital_id = UUID::Parse(start_vital_id_str);
    REQUIRE(start_vital_id.has_value());
    REQUIRE(*end_vital_id != *start_vital_id);
    REQUIRE(end_ev["vital"].count("failure_reason") == 0);
  }

  SECTION("M emit end vital event with failure_reason W FailOperation is processed") {
    // Given an active session with a view and an active operation
    StartView();
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "upload", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // When we fail the operation with an error reason
    scope.Process(
        RumCommand::StopOperation(
            GetBaseParams(), "upload", std::nullopt, RumOperationFailureReason::Error
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // Then the end event includes failure_reason
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    const auto& start_ev = vitals[0];
    const auto& end_ev = vitals[1];
    REQUIRE(end_ev["vital"]["step_type"] == "end");
    REQUIRE(end_ev["vital"]["failure_reason"] == "error");
    // End vital ID is distinct from start vital ID
    const std::string start_vital_id_str = start_ev["vital"]["id"];
    const std::string end_vital_id_str = end_ev["vital"]["id"];
    const auto start_vital_id = UUID::Parse(start_vital_id_str);
    const auto end_vital_id = UUID::Parse(end_vital_id_str);
    REQUIRE(start_vital_id.has_value());
    REQUIRE(end_vital_id.has_value());
    REQUIRE(*end_vital_id != *start_vital_id);
  }

  SECTION("M include operation_key W operation_key is provided") {
    StartView();
    scope.Process(
        RumCommand::StartOperation(
            GetBaseParams(), "checkout", std::string_view{"cart-42"}
        ),
        GetTestContext(),
        GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    REQUIRE(vitals[0]["vital"]["operation_key"] == "cart-42");
  }

  SECTION("M emit vital with abandoned failure_reason W abandoned") {
    StartView();
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "login", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    scope.Process(
        RumCommand::StopOperation(
            GetBaseParams(), "login", std::nullopt, RumOperationFailureReason::Abandoned
        ),
        GetTestContext(),
        GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    const auto& start_ev = vitals[0];
    const auto& end_ev = vitals[1];
    REQUIRE(end_ev["vital"]["failure_reason"] == "abandoned");
    // End vital ID is distinct from start vital ID
    const std::string start_vital_id_str = start_ev["vital"]["id"];
    const std::string end_vital_id_str = end_ev["vital"]["id"];
    const auto start_vital_id = UUID::Parse(start_vital_id_str);
    const auto end_vital_id = UUID::Parse(end_vital_id_str);
    REQUIRE(start_vital_id.has_value());
    REQUIRE(end_vital_id.has_value());
    REQUIRE(*end_vital_id != *start_vital_id);
  }

  SECTION("M warn on duplicate start W same operation started twice") {
    StartView();
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // Start the same operation again
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // Both events are emitted (warnings never suppress events)
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    // A warning was logged including the operation name
    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("checkout") != std::string::npos);
    REQUIRE(warnings[0].find("has already been started") != std::string::npos);
  }

  SECTION("M warn on stop without start W operation stopped without matching start") {
    StartView();
    scope.Process(
        RumCommand::StopOperation(
            GetBaseParams(), "unknown-op", std::nullopt, std::nullopt
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // Event is still emitted despite no matching start
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    REQUIRE(vitals[0]["vital"]["step_type"] == "end");
    // A warning was logged including the operation name
    auto& warnings = event_capture.Diagnostics().warning;
    REQUIRE(warnings.size() == 1);
    REQUIRE(warnings[0].find("unknown-op") != std::string::npos);
    REQUIRE(warnings[0].find("not currently active") != std::string::npos);
  }

  SECTION("M emit vital event with zero view ID W no active view exists") {
    // Given an active session with NO views
    // When we process a StartOperation command
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "background-op", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // Then a vital event is still emitted with zero-valued view
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    REQUIRE(vitals[0]["view"]["id"] == "00000000-0000-0000-0000-000000000000");
    REQUIRE(vitals[0]["view"]["url"] == "");
  }

  SECTION("M track parallel operations with distinct keys correctly") {
    StartView();

    // Start two operations with same name but different operation_keys
    scope.Process(
        RumCommand::StartOperation(
            GetBaseParams(), "upload", std::string_view{"file-1"}
        ),
        GetTestContext(),
        GetTestWriter()
    );
    scope.Process(
        RumCommand::StartOperation(
            GetBaseParams(), "upload", std::string_view{"file-2"}
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // No warnings - these are distinct operations
    auto& warnings = event_capture.Diagnostics().warning;
    auto vitals = event_capture.Vitals();
    REQUIRE(warnings.size() == 0);
    REQUIRE(vitals.size() == 2);

    // Stop one
    scope.Process(
        RumCommand::StopOperation(
            GetBaseParams(), "upload", std::string_view{"file-1"}, std::nullopt
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // No warning for stop-with-matching-start
    vitals = event_capture.Vitals();
    REQUIRE(warnings.size() == 0);
    REQUIRE(vitals.size() == 3);
  }

  SECTION("M clear active operations W session is stopped") {
    StartView();

    // Start an operation
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // Stop the session (this clears active operations)
    scope.Process(
        RumCommand::StopSession(GetBaseParams()), GetTestContext(), GetTestWriter()
    );

    // The session ended, so the test verifies no crash occurred and state was cleaned
    // up
    REQUIRE(scope.GetEndReason().has_value());
  }

  SECTION("M include merged attributes in vital event context") {
    // Given global attributes on the command and a view with attributes
    StartView();

    // Create command params with custom attributes
    Attribute cmd_attrs = Attribute::Object();
    cmd_attrs.SetObjectProperty("command.key", Attribute::String("cmd-val"));
    auto params = RumCommandParams(clock.Now(), {}, cmd_attrs);

    scope.Process(
        RumCommand::StartOperation(std::move(params), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    const auto& ev = vitals[0];
    REQUIRE(ev.count("context") == 1);
    REQUIRE(ev["context"]["command.key"] == "cmd-val");
  }

  SECTION(
      "M capture view context at emission time W view becomes active mid-operation"
  ) {
    // EDGE-02: vital events capture the *current* view context at the moment they are
    // emitted, not the view context at operation start.

    // Given no active view - start event has zero view ID
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );
    auto vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 1);
    REQUIRE(vitals[0]["view"]["id"] == "00000000-0000-0000-0000-000000000000");

    // When a view is started mid-operation
    StartView();

    // And the operation is stopped
    scope.Process(
        RumCommand::StopOperation(
            GetBaseParams(), "checkout", std::nullopt, std::nullopt
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // Then the stop event captures the current (non-zero) view context
    vitals = event_capture.Vitals();
    REQUIRE(vitals.size() == 2);
    const auto& start_ev = vitals[0];
    const auto& end_ev = vitals[1];
    REQUIRE(end_ev["vital"]["step_type"] == "end");
    REQUIRE(end_ev["view"]["id"] != "00000000-0000-0000-0000-000000000000");
    // End vital ID is distinct from start vital ID
    const std::string start_vital_id_str = start_ev["vital"]["id"];
    const std::string end_vital_id_str = end_ev["vital"]["id"];
    const auto start_vital_id = UUID::Parse(start_vital_id_str);
    const auto end_vital_id = UUID::Parse(end_vital_id_str);
    REQUIRE(start_vital_id.has_value());
    REQUIRE(end_vital_id.has_value());
    REQUIRE(*end_vital_id != *start_vital_id);
  }

  SECTION("M not extend session timeout W operation command is processed") {
    // Given a session that is near the inactivity timeout
    clock.Tick(std::chrono::minutes(14));

    // When a non-UserInteraction command (operation) is processed
    scope.Process(
        RumCommand::StartOperation(GetBaseParams(), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // And then another minute passes
    clock.Tick(std::chrono::minutes(2));

    // Then the session should expire because operations don't refresh inactivity
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "foo", ""),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(
        scope.GetEndReason().value() ==
        RumSessionScope::EndReason::TimedOutDueToInactivity
    );
  }
}

TEST_CASE("RumSessionScope::PopulateContext", "[unit][rum]") {
  SECTION("M set application_id and session_id W session is active") {
    // Given a RumApplicationScope configured with a specific app ID
    RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
    RumScopeDependencies deps(config, MockClock());
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
        Timestamp{},
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
    RumScopeDependencies deps(config, MockClock());
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
        Timestamp{},
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
    RumScopeDependencies deps(config, MockClock());
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
        Timestamp{},
        std::nullopt
    );
    CoreContext local_context = MOCK_CONTEXT;
    EventWriter local_writer = [](Block, Block, bool) { return true; };

    // When the session ends for any reason
    scope.Process(
        RumCommand::StopSession(RumCommandParams(Timestamp{}, {}, {})),
        local_context,
        local_writer
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
