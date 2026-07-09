// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scopes/action.hpp"

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
#include "datadog/impl/rum/scopes/session.hpp"
#include "datadog/impl/rum/scopes/view.hpp"

#include "mock/clock.hpp"
#include "support/rum_event_capture.hpp"

using namespace datadog;
using namespace datadog::impl;

class ActionFixture {
 protected:
  static constexpr const char* APPLICATION_ID = "a991ca10-4004-4004-4004-beefbeefbeef";
  static constexpr const char* SESSION_ID = "5e551017-4114-4114-4114-beeeefbeeeef";
  static constexpr const char* VIEW_ID = "141ee144-4224-4224-4224-beeeeeeeeeef";
  static constexpr const char* ACTION_ID = "4c10171e-4334-4334-4334-b0000eeeefff";

  MockClock clock;

  RumConfig config;
  RumScopeDependencies deps;
  RumApplicationScope root;
  RumSessionScope session;
  RumViewScope parent;
  RumActionScope scope;

  RumEventCapture event_capture;

 public:
  CoreContext GetTestContext() { return event_capture.GetContext(); }
  EventWriter GetTestWriter() { return event_capture.GetWriter(); }
  ActionFixture()
      : config(APPLICATION_ID),
        deps(config, clock),
        root(deps),
        session(
            deps,
            root,
            true,
            true,
            *UUID::Parse(SESSION_ID),
            RumSessionPrecondition::UserAppLaunch,
            Timestamp{std::chrono::duration_cast<Duration>(
                std::chrono::milliseconds{1700000000000}
            )},
            std::nullopt
        ),
        parent(
            deps,
            session,
            true,
            *UUID::Parse(VIEW_ID),
            "my-view-key",
            "My View Name",
            Timestamp{std::chrono::duration_cast<Duration>(
                std::chrono::milliseconds{1700000000000}
            )}
        ),
        scope(
            deps,
            parent,
            *UUID::Parse(ACTION_ID),
            RumActionType::Click,
            "button1",
            Timestamp{std::chrono::duration_cast<Duration>(
                std::chrono::milliseconds{1700000000000}
            )},
            std::chrono::milliseconds(100),
            Attribute()
        ),
        event_capture(APPLICATION_ID, SESSION_ID, VIEW_ID) {
    deps.diagnostic_logger = event_capture.GetFeatureScope().diagnostic_logger;
    clock.FreezeAtMilliseconds(1700000000000);
  }

  RumCommandParams GetBaseParams() { return RumCommandParams(clock.Now(), {}, {}); }
};

TEST_CASE_METHOD(ActionFixture, "RumActionScope::Process", "[unit][rum]") {
  SECTION("M close and send event W StopSession is processed") {
    // When we process StopSession
    const auto result = scope.Process(
        RumCommand::StopSession(GetBaseParams()), GetTestContext(), GetTestWriter()
    );

    // Then our action scope is closed
    REQUIRE(result == RumScopeResult::Close);

    // And a single event is generated to describe the action
    auto actions = event_capture.Actions();
    REQUIRE(actions.size() == 1);
    const auto& event = actions.front();
    REQUIRE(event["action"]["loading_time"] == 0);
  }

  SECTION("M close and send event W StartView is processed") {
    // When we process StartView with _any_ view key, at T+5ms
    clock.TickMilliseconds(5);
    const auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "some-view", "Some View"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our action scope is closed
    REQUIRE(result == RumScopeResult::Close);

    // And a single event is generated to describe the action
    auto actions = event_capture.Actions();
    REQUIRE(actions.size() == 1);

    // And the action's duration (in ns) reflects the 5ms for which the scope was open
    const auto& event = actions.front();
    REQUIRE(event["action"]["loading_time"] == 5000000);
  }

  SECTION("M close and send event W StopView is processed") {
    // When we process StopView
    const auto result = scope.Process(
        RumCommand::StopView(GetBaseParams(), "some-view"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our action scope is closed
    REQUIRE(result == RumScopeResult::Close);

    // And a single event is generated to describe the action
    REQUIRE(event_capture.Actions().size() == 1);
  }

  SECTION("M remain open W any other command is processed prior to timeout") {
    // Given a RumActionScope with a configured timeout of 100ms

    // When we process StartResource at T+90ms
    const auto result = scope.Process(
        RumCommand::StartResource(
            GetBaseParams(),
            "some-resource",
            RumRequestDetails{RumResourceMethod::Get, "http://localhost:5000/foo"}
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our action scope remains open
    REQUIRE(result == RumScopeResult::RemainOpen);

    // And no event is produced yet
    REQUIRE(event_capture.Actions().empty());
  }

  SECTION("M close and send event W any other command is processed after timeout") {
    // Given a RumActionScope with a configured timeout of 100ms

    // When we process StartResource at T+110ms
    clock.TickMilliseconds(110);
    const auto result = scope.Process(
        RumCommand::StartResource(
            GetBaseParams(),
            "some-resource",
            RumRequestDetails{RumResourceMethod::Get, "http://localhost:5000/foo"}
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our action scope is closed, because the command was processed after the
    // timeout interval had passed
    REQUIRE(result == RumScopeResult::Close);

    // And an event is produced
    auto actions = event_capture.Actions();
    REQUIRE(actions.size() == 1);

    // And the event shows that no resources were recorded during the lifetime of the
    // action: our StartResource call doesn't count
    const auto& event = actions.front();
    REQUIRE(!event["action"].contains("resource"));

    // And the action's duration is backdated to 100ms
    REQUIRE(event["action"]["loading_time"] == 100000000);
  }

  SECTION("M remain open until resource completion W resource is pending") {
    // Given a RumActionScope with a configured timeout of 100ms

    // When we process StartResource at T+50ms
    clock.TickMilliseconds(50);
    auto result = scope.Process(
        RumCommand::StartResource(
            GetBaseParams(),
            "some-resource",
            RumRequestDetails{RumResourceMethod::Get, "http://localhost:5000/foo"}
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our action scope remains open
    REQUIRE(result == RumScopeResult::RemainOpen);

    // Next: When we process StopResource at T+150ms
    clock.TickMilliseconds(100);
    result = scope.Process(
        RumCommand::StopResource(GetBaseParams(), "some-resource"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our scope is closed, because the timeout has passed and we no longer have
    // pending resources
    REQUIRE(result == RumScopeResult::Close);

    // And an event is produced that records our completed resource, despite the fact
    // that the StopResource call occurred after our expected timeout duration
    auto actions = event_capture.Actions();
    const auto& event = actions.front();
    REQUIRE(event["action"]["resource"]["count"] == 1);
    REQUIRE(event["action"]["loading_time"] == 100000000);
  }

  SECTION(
      "M remain open as long as any resource is pending W multiple pending resource "
      "are chained"
  ) {
    // Given this series of calls:
    // - T+50ms: StartResource("foo")
    // - T+150ms: StartResource("bar")
    // - T+500ms: StopResource("foo")
    // - T+900ms: StopResource("bar")
    const RumRequestDetails req{RumResourceMethod::Get, "http://localhost:5000/foo"};

    // When we start a resource 'foo' before timeout, Then our action scope remains open
    clock.TickMilliseconds(50);
    auto result = scope.Process(
        RumCommand::StartResource(GetBaseParams(), "foo", req),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::RemainOpen);

    // When we start a resource 'bar' at any point thereafter, so long as 'foo' is still
    // pending, Then our action scope remains open
    clock.TickMilliseconds(100);
    result = scope.Process(
        RumCommand::StartResource(GetBaseParams(), "bar", req),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::RemainOpen);

    // When we stop either one of those two resources, Then our action scope still
    // remains open, because there's still another resource pending
    clock.TickMilliseconds(350);
    result = scope.Process(
        RumCommand::StopResource(GetBaseParams(), "foo"),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::RemainOpen);

    // When we stop the final resource, Then our action scope finally closes and sends
    // an event that records 2 resources
    clock.TickMilliseconds(400);
    REQUIRE(event_capture.Actions().empty());
    result = scope.Process(
        RumCommand::StopResource(GetBaseParams(), "bar"),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::Close);
    auto actions = event_capture.Actions();
    REQUIRE(actions.size() == 1);
    REQUIRE(actions.front()["action"]["resource"]["count"] == 2);
  }

  SECTION("M increment error count W resource is stopped due to error") {
    // Given a RumActionScope with a configured timeout of 100ms

    // When we process StartResource at T+0
    auto result = scope.Process(
        RumCommand::StartResource(
            GetBaseParams(),
            "some-resource",
            RumRequestDetails{RumResourceMethod::Get, "http://localhost:5000/foo"}
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our action scope remains open
    REQUIRE(result == RumScopeResult::RemainOpen);

    // Next: When we process StopResource with valid error details at T+50ms
    clock.TickMilliseconds(50);
    result = scope.Process(
        RumCommand::StopResource(
            GetBaseParams(), "some-resource", RumResponseDetails(), RumErrorDetails()
        ),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our scope remains open, because we haven't exceeded the timeout
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(event_capture.Actions().empty());

    // Next: When the action scope is closed for any reason and sends its action event
    clock.TickMilliseconds(3);
    result = scope.Process(
        RumCommand::StopSession(GetBaseParams()), GetTestContext(), GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::Close);
    auto actions = event_capture.Actions();
    REQUIRE(actions.size() == 1);

    // Then our resource count and error counts are both 1
    const auto& event = actions.front();
    REQUIRE(event["action"]["resource"]["count"] == 1);
    REQUIRE(event["action"]["error"]["count"] == 1);
    REQUIRE(event["action"]["loading_time"] == 53000000);
  }

  SECTION("M increment error count W explicit error is reported") {
    // Given an active RumActionScope

    // When we process AddError
    auto result = scope.Process(
        RumCommand::AddError(
            GetBaseParams(),
            RumErrorSource::Source,
            RumErrorDetails{"oops", "Error", "stacktrace"}
        ),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::RemainOpen);

    // And we then process StopAction to explicitly end the action 10ms later
    clock.TickMilliseconds(10);
    result = scope.Process(
        RumCommand::StopAction(GetBaseParams(), ""), GetTestContext(), GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::Close);
    auto actions = event_capture.Actions();
    REQUIRE(actions.size() == 1);

    // Then we get an action event that has an error count of 1
    const auto& event = actions.front();
    REQUIRE(event["action"]["error"]["count"] == 1);
    REQUIRE(event["action"]["loading_time"] == 10000000);
  }

  SECTION("M increment long_task count W long task is reported") {
    // Given an active RumActionScope

    // When we process AddLongTask
    auto result = scope.Process(
        RumCommand::AddLongTask(GetBaseParams(), std::chrono::milliseconds(50)),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::RemainOpen);

    // And we then process StopAction to explicitly end the action 10ms later
    clock.TickMilliseconds(10);
    result = scope.Process(
        RumCommand::StopAction(GetBaseParams(), ""), GetTestContext(), GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::Close);
    auto actions = event_capture.Actions();
    REQUIRE(actions.size() == 1);

    // Then we get an action event that has a long_task count of 1
    const auto& event = actions.front();
    REQUIRE(event["action"]["long_task"]["count"] == 1);
    REQUIRE(event["action"]["loading_time"] == 10000000);
  }
}

TEST_CASE("RumActionScope::PopulateContext", "[unit][rum]") {
  SECTION("M set active_view_id") {
    SECTION("M set application_id and session_id W session is active") {
      // Given a RumApplicationScope
      RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
      RumScopeDependencies deps(config, MockClock());
      RumApplicationScope application_scope(deps);

      // And a RumSessionScope
      const bool is_initial_session = true;
      const bool is_sampled = true;
      const UUID session_id = *UUID::Parse("689d0d6c-c716-4eed-b449-0df936a615f8");
      RumSessionScope session_scope(
          deps,
          application_scope,
          is_initial_session,
          is_sampled,
          session_id,
          RumSessionPrecondition::UserAppLaunch,
          Timestamp{},
          std::nullopt
      );

      // And a RumViewScope
      const bool is_initial_view = true;
      const UUID view_id = *UUID::Parse("4789b6fe-f52c-4437-a087-5f16b4f9e28b");
      std::string_view view_key = "my-view";
      std::string_view view_name = "My View";
      RumViewScope view_scope(
          deps,
          session_scope,
          is_initial_view,
          view_id,
          view_key,
          view_name,
          Timestamp{}
      );

      // And a RumActionScope
      const UUID action_id = *UUID::Parse("4cf8f1b7-845e-48b0-9ae1-5f2a915ea970");
      const RumActionType action_type = RumActionType::Custom;
      std::string_view action_name = "my-action";
      RumActionScope scope(
          deps,
          view_scope,
          action_id,
          action_type,
          action_name,
          Timestamp{},
          std::chrono::milliseconds(100),
          Attribute()
      );

      // When we populate a RumContext from the action scope
      RumContext ctx;
      scope.PopulateContext(ctx);

      // Then it has all expected values
      REQUIRE(
          ctx.application_id == *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef")
      );
      REQUIRE(ctx.session_id == session_id);
      REQUIRE(ctx.session_is_sampled == true);
      REQUIRE(ctx.session_is_active == true);
      REQUIRE(ctx.session_precondition == RumSessionPrecondition::UserAppLaunch);
      REQUIRE(ctx.active_view_id == view_id);
      REQUIRE(ctx.active_view_key == view_key);
      REQUIRE(ctx.active_view_name == view_name);
      REQUIRE(ctx.active_action_id == action_id);
    }
  }
}
