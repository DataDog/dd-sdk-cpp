// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scopes/view.hpp"

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

#include "mock/clock.hpp"
#include "support/rum_event_capture.hpp"

using namespace datadog;
using namespace datadog::impl;

class ViewFixture {
 protected:
  static constexpr const char* APPLICATION_ID = "a991ca10-4004-4004-4004-beefbeefbeef";
  static constexpr const char* SESSION_ID = "5e551017-4114-4114-4114-beeeefbeeeef";
  static constexpr const char* VIEW_ID = "141ee144-4224-4224-4224-beeeeeeeeeef";

  MockClock clock;

  RumConfig config;
  RumScopeDependencies deps;
  RumApplicationScope root;
  RumSessionScope parent;
  RumViewScope scope;

  RumEventCapture event_capture;

 public:
  CoreContext GetTestContext() { return event_capture.GetContext(); }
  EventWriter GetTestWriter() { return event_capture.GetWriter(); }
  ViewFixture()
      : config(APPLICATION_ID),
        deps(config, clock),
        root(deps),
        parent(
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
        scope(
            deps,
            parent,
            true,
            *UUID::Parse(VIEW_ID),
            "my-view-key",
            "My View Name",
            Timestamp{std::chrono::duration_cast<Duration>(
                std::chrono::milliseconds{1700000000000}
            )}
        ),
        event_capture(APPLICATION_ID, SESSION_ID, VIEW_ID) {
    deps.diagnostic_logger = event_capture.GetFeatureScope().diagnostic_logger;
    clock.FreezeAtMilliseconds(1700000000000);
  }

  RumCommandParams GetBaseParams() { return RumCommandParams(clock.Now(), {}, {}); }
};

TEST_CASE_METHOD(ViewFixture, "RumSessionScope::Process", "[unit][rum]") {
  SECTION("M deactivate and send final event W StopSession is processed") {
    // Given an active RumViewScope
    REQUIRE(scope.IsActive() == true);

    // When we process StopSession
    const auto result = scope.Process(
        RumCommand::StopSession(GetBaseParams()), GetTestContext(), GetTestWriter()
    );

    // Then the view scope is rendered inactive
    REQUIRE(scope.IsActive() == false);

    // And the view scope is also closed, since it has no pending resources
    REQUIRE(result == RumScopeResult::Close);

    // And a single event is generated to describe the final state of the view
    auto views = event_capture.Views();
    REQUIRE(event_capture.FilterByType("resource").empty());
    REQUIRE(views.size() == 1);
    REQUIRE(views[0]["view"]["is_active"] == false);
  }

  SECTION(
      "M remain active and send initial event W initial StartView is processed and key "
      "matches"
  ) {
    // When we process an initial StartView command whose key matches ours
    const auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "my-view-key", "My View Name"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then the view remains active and open
    REQUIRE(scope.IsActive() == true);
    REQUIRE(result == RumScopeResult::RemainOpen);

    // And a single event is generated to describe the initial state of the view
    auto views = event_capture.Views();
    REQUIRE(event_capture.FilterByType("resource").empty());
    REQUIRE(views.size() == 1);
    REQUIRE(views[0]["view"]["is_active"] == true);
  }

  SECTION(
      "M deactivate and send a final event W second StartView with same key is "
      "processed"
  ) {
    // Given a view scope that's already seen an initial StartView command
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "my-view-key", "My View Name"),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(scope.IsActive() == true);
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(event_capture.Views().size() == 1);

    // When we process another StartView command whose key is the same
    result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "my-view-key", "My View Name"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then the original view scope is deactivated (and in this case, with no pending
    // resources, it's also closed) because we presume that the StartView command was
    // issued in response to a different view with the same key being opened
    REQUIRE(scope.IsActive() == false);
    REQUIRE(result == RumScopeResult::Close);

    // And the original view generated two events over its lifetime: the initial view
    // event sent on open, and the final view event sent on close
    auto views = event_capture.Views();
    REQUIRE(event_capture.FilterByType("resource").empty());
    REQUIRE(views.size() == 2);
    REQUIRE(views[0]["view"]["is_active"] == true);
    REQUIRE(views[1]["view"]["is_active"] == false);
  }

  SECTION(
      "M deactivate and send a final event W StartView is processed and key does not "
      "match"
  ) {
    // Given an active view scope with 'my-view-key'
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "my-view-key", "My View Name"),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(scope.IsActive() == true);
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(event_capture.Views().size() == 1);

    // When we process a StartView command with a different view key
    result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "a-different-view-key", "Different!"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our original scope is deactivated (and closed), as the newly-created view
    // will have taken the title of active view
    REQUIRE(scope.IsActive() == false);
    REQUIRE(result == RumScopeResult::Close);

    // And the original view generated two events over its lifetime: the initial view
    // event sent on open, and the final view event sent on close
    auto views = event_capture.Views();
    REQUIRE(event_capture.FilterByType("resource").empty());
    REQUIRE(views.size() == 2);
    REQUIRE(views[0]["view"]["is_active"] == true);
    REQUIRE(views[1]["view"]["is_active"] == false);
  }

  SECTION("M deactivate W StartView key does not match, event if not started") {
    // Given an active view scope with 'my-view-key' that never got an initial StartView
    REQUIRE(scope.IsActive() == true);

    // When we process a StartView command with a different view key
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "a-different-view-key", "Different!"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our original scope is deactivated and closed, irrespective of whether it
    // ever received StartView
    REQUIRE(scope.IsActive() == false);
    REQUIRE(result == RumScopeResult::Close);

    // And it generates a final view event on close
    auto views = event_capture.Views();
    REQUIRE(event_capture.FilterByType("resource").empty());
    REQUIRE(views.size() == 1);
    REQUIRE(views[0]["view"]["is_active"] == false);
  }

  SECTION("M deactivate and send a final event W StopView has matching key") {
    // Given an active view scope with 'my-view-key'
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "my-view-key", "My View Name"),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(scope.IsActive() == true);
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(event_capture.Views().size() == 1);

    // When we process a StopView command that matches 'my-view-key'
    result = scope.Process(
        RumCommand::StopView(GetBaseParams(), "my-view-key"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our scope is deactivated (and closed), and it generates an event on close
    REQUIRE(scope.IsActive() == false);
    REQUIRE(result == RumScopeResult::Close);
    auto views = event_capture.Views();
    REQUIRE(event_capture.FilterByType("resource").empty());
    REQUIRE(views.size() == 2);
    REQUIRE(views[0]["view"]["is_active"] == true);
    REQUIRE(views[1]["view"]["is_active"] == false);
  }

  SECTION("M do nothing W StopView has non-matching key") {
    // Given an active view scope with 'my-view-key'
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "my-view-key", "My View Name"),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(scope.IsActive() == true);
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(event_capture.Views().size() == 1);

    // When we process a StopView command that targets a different view key
    result = scope.Process(
        RumCommand::StopView(GetBaseParams(), "different-key"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our scope remains active and open, and it sends no additional events
    REQUIRE(scope.IsActive() == true);
    REQUIRE(result == RumScopeResult::RemainOpen);
    auto views = event_capture.Views();
    REQUIRE(event_capture.FilterByType("resource").empty());
    REQUIRE(views.size() == 1);
    REQUIRE(views[0]["view"]["is_active"] == true);
  }

  SECTION("M remain open until pending resources finish W StopView called") {
    // Given an active view scope with 'my-view-key'
    auto result = scope.Process(
        RumCommand::StartView(GetBaseParams(), "my-view-key", "My View Name"),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(scope.IsActive() == true);
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(event_capture.Views().size() == 1);

    // And an active resource scope with 'my-resource-key'
    result = scope.Process(
        RumCommand::StartResource(
            GetBaseParams(),
            "my-resource-key",
            RumRequestDetails{RumResourceMethod::Get, "/foo"}
        ),
        GetTestContext(),
        GetTestWriter()
    );
    REQUIRE(result == RumScopeResult::RemainOpen);
    REQUIRE(event_capture.Views().size() == 1);

    // When we process a StopView command that matches 'my-view-key'
    result = scope.Process(
        RumCommand::StopView(GetBaseParams(), "my-view-key"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our scope is deactivated, and it generates an event, but:
    // - the scope remains open due to the pending resource
    // - the scope shows IsActive() == false internally
    // - but the event continues to show view.is_active == true
    REQUIRE(scope.IsActive() == false);
    REQUIRE(result == RumScopeResult::RemainOpen);
    auto views = event_capture.Views();
    REQUIRE(event_capture.FilterByType("resource").empty());
    REQUIRE(views.size() == 2);
    REQUIRE(views[0]["view"]["is_active"] == true);
    REQUIRE(views[1]["view"]["is_active"] == true);

    // Next: when we provide the same (now-inactive but still-open) RumViewScope with a
    // command that stops our pending resource
    result = scope.Process(
        RumCommand::StopResource(GetBaseParams(), "my-resource-key"),
        GetTestContext(),
        GetTestWriter()
    );

    // Then our scope is finally closed, and it sends a final event that records the
    // final state of the view with is_active == false and 1 completed resource
    REQUIRE(scope.IsActive() == false);
    REQUIRE(result == RumScopeResult::Close);
    views = event_capture.Views();
    REQUIRE(event_capture.FilterByType("resource").size() == 1);
    REQUIRE(views.size() == 3);
    REQUIRE(views[2]["view"]["is_active"] == false);
    REQUIRE(views[2]["view"]["resource"]["count"] == 1);
  }

  SECTION(
      "M ignore event and send no event W lifecycle command is processed while already "
      "inactive"
  ) {
    // Given a range of different RumCommand values
    struct TestParams {
      std::string_view name;
      std::function<RumCommand()> cmd_thunk;
    };
    std::vector<TestParams> tests = {
        {"StartView with same key",
         [this]() {
           return RumCommand::StartView(GetBaseParams(), "my-view-key", "");
         }},
        {"StartView with different key",
         [this]() {
           return RumCommand::StartView(GetBaseParams(), "different-view-key", "");
         }},
        {"StopView with same key",
         [this]() { return RumCommand::StopView(GetBaseParams(), "my-view-key"); }},
        {"StopSession", [this]() { return RumCommand::StopSession(GetBaseParams()); }}
    };

    for (const auto& tt : tests) {
      // Given a specific type of command that affects view lifecycle
      DYNAMIC_SECTION(" {" << tt.name << "}") {
        // And a scope that's been rendered inactive by a StopView call
        scope.Process(
            RumCommand::StartView(GetBaseParams(), "my-view-key", ""),
            GetTestContext(),
            GetTestWriter()
        );
        scope.Process(
            RumCommand::StopView(GetBaseParams(), "my-view-key"),
            GetTestContext(),
            GetTestWriter()
        );
        REQUIRE(scope.IsActive() == false);
        auto views = event_capture.Views();
        REQUIRE(views.size() == 2);
        REQUIRE(views[0]["view"]["is_active"] == true);
        REQUIRE(views[1]["view"]["is_active"] == false);

        // When we process a command that would ordinarily affect this view's lifecycle
        // and cause it to generate an event
        RumCommand cmd = tt.cmd_thunk();
        auto result = scope.Process(cmd, GetTestContext(), GetTestWriter());

        // Then the command is ignored, and no new events are generated, because the
        // view is already inactive
        REQUIRE(result == RumScopeResult::Close);
        REQUIRE(scope.IsActive() == false);
        REQUIRE(event_capture.Views().size() == 2);
      }
    }
  }
}

TEST_CASE("RumViewScope::PopulateContext", "[unit][rum]") {
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
      RumViewScope scope(
          deps,
          session_scope,
          is_initial_view,
          view_id,
          view_key,
          view_name,
          Timestamp{}
      );

      // When we populate a RumContext from the view scope
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
    }
  }
}
