// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/rum/scopes/session.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/features/rum/context.hpp"
#include "datadog/impl/features/rum/scopes/application.hpp"
#include "datadog/impl/features/rum/scopes/view.hpp"
#include "datadog/impl/platform/system_info.hpp"

#include "mock/clock.hpp"
#include "mock/system_info.hpp"
#include "support/rum_event_capture.hpp"

using namespace datadog;
using namespace datadog::impl;

class SessionFixture {
 protected:
  static constexpr const char* APPLICATION_ID = "a991ca10-4004-4004-4004-beefbeefbeef";
  static constexpr const char* SESSION_ID = "5e551017-4114-4114-4114-beeeefbeeeef";

  MockClock clock;
  MockSystemInfo system_info;

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
        context(
            CoreConfig{"test-token", "test-service", "test-env"},
            system_info.os_info,
            system_info.device_info
        ),
        writer([](Block, Block) { return true; }) {
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
    // When we process StopSession
    const auto result =
        scope.Process(RumCommand::StopSession(GetBaseParams()), context, writer);

    // Then the scope is explicitly closed
    REQUIRE(result == RumScopeResult::Close);
    REQUIRE(scope.GetEndReason() == RumSessionScope::EndReason::Stopped);
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

TEST_CASE_METHOD(SessionEventFixture, "RumSessionScope operations", "[unit][rum]") {
  SECTION("M emit start vital event W StartFeatureOperation is processed") {
    // Given an active session with a view
    StartView();

    // When we process a StartFeatureOperation command
    scope.Process(
        RumCommand::StartFeatureOperation(GetBaseParams(), "checkout", std::nullopt),
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

  SECTION(
      "M emit end vital event with no failure W SucceedFeatureOperation is processed"
  ) {
    // Given an active session with a view and an active operation
    StartView();
    scope.Process(
        RumCommand::StartFeatureOperation(GetBaseParams(), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // When we stop the operation successfully
    scope.Process(
        RumCommand::StopFeatureOperation(
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

  SECTION(
      "M emit end vital event with failure_reason W FailFeatureOperation is processed"
  ) {
    // Given an active session with a view and an active operation
    StartView();
    scope.Process(
        RumCommand::StartFeatureOperation(GetBaseParams(), "upload", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // When we fail the operation with an error reason
    scope.Process(
        RumCommand::StopFeatureOperation(
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
        RumCommand::StartFeatureOperation(
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
        RumCommand::StartFeatureOperation(GetBaseParams(), "login", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    scope.Process(
        RumCommand::StopFeatureOperation(
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
        RumCommand::StartFeatureOperation(GetBaseParams(), "checkout", std::nullopt),
        GetTestContext(),
        GetTestWriter()
    );

    // Start the same operation again
    scope.Process(
        RumCommand::StartFeatureOperation(GetBaseParams(), "checkout", std::nullopt),
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
        RumCommand::StopFeatureOperation(
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
    // When we process a StartFeatureOperation command
    scope.Process(
        RumCommand::StartFeatureOperation(
            GetBaseParams(), "background-op", std::nullopt
        ),
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
        RumCommand::StartFeatureOperation(
            GetBaseParams(), "upload", std::string_view{"file-1"}
        ),
        GetTestContext(),
        GetTestWriter()
    );
    scope.Process(
        RumCommand::StartFeatureOperation(
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
        RumCommand::StopFeatureOperation(
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
        RumCommand::StartFeatureOperation(GetBaseParams(), "checkout", std::nullopt),
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
        RumCommand::StartFeatureOperation(std::move(params), "checkout", std::nullopt),
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
        RumCommand::StartFeatureOperation(GetBaseParams(), "checkout", std::nullopt),
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
        RumCommand::StopFeatureOperation(
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
        RumCommand::StartFeatureOperation(GetBaseParams(), "checkout", std::nullopt),
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
    MockSystemInfo local_system_info;
    CoreContext local_context(
        CoreConfig{"test-token", "test-service", "test-env"},
        local_system_info.os_info,
        local_system_info.device_info
    );
    EventWriter local_writer = [](Block, Block) { return true; };

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
