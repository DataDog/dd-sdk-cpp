// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/rum/scopes/resource.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/features/rum/context.hpp"
#include "datadog/impl/features/rum/scopes/application.hpp"
#include "datadog/impl/features/rum/scopes/session.hpp"
#include "datadog/impl/features/rum/scopes/view.hpp"
#include "datadog/impl/platform/system_info.hpp"

#include "mock/clock.hpp"
#include "support/rum_event_capture.hpp"

using namespace datadog;
using namespace datadog::impl;

class ResourceFixture {
 protected:
  static constexpr const char* APPLICATION_ID = "a991ca10-4004-4004-4004-beefbeefbeef";
  static constexpr const char* SESSION_ID = "5e551017-4114-4114-4114-beeeefbeeeef";
  static constexpr const char* VIEW_ID = "141ee144-4224-4224-4224-beeeeeeeeeef";
  static constexpr const char* RESOURCE_ID = "8e5048ce-4444-4444-4444-bbb000eeefff";

  MockClock clock;

  RumConfig config;
  RumScopeDependencies deps;
  RumApplicationScope root;
  RumSessionScope session;
  RumViewScope parent;
  RumResourceScope scope;

  RumEventCapture event_capture;

 public:
  ResourceFixture()
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
            *UUID::Parse(RESOURCE_ID),
            "my-resource-key",
            RumResourceMethod::Get,
            "https://www.example.com/api/foo",
            Timestamp{std::chrono::duration_cast<Duration>(
                std::chrono::milliseconds{1700000000000}
            )},
            Attribute()
        ),
        event_capture(APPLICATION_ID, SESSION_ID, VIEW_ID) {
    deps.scope = &event_capture.GetFeatureScope();
    deps.diagnostic_logger = event_capture.GetFeatureScope().diagnostic_logger;
    clock.FreezeAtMilliseconds(1700000000000);
  }

  RumCommandParams GetBaseParams() { return RumCommandParams(clock.Now(), {}, {}); }
};

TEST_CASE_METHOD(ResourceFixture, "RumResourceScope::Process", "[unit][rum]") {
  SECTION(
      "M close and send resource event W StopResource is processed with matching key, "
      "no error details"
  ) {
    // Given a resource that's been active for 5ms
    clock.TickMilliseconds(5);

    // When we process a StopResource command that targets our resource scope and that
    // doesn't include any error details
    const auto result = scope.Process(
        RumCommand::StopResource(
            GetBaseParams(),
            "my-resource-key",
            RumResponseDetails{403, 16, RumResourceType::Other}
        )
    );

    // Then our resource scope is closed
    REQUIRE(result == RumScopeResult::Close);

    // And a single 'resource' event is generated to describe the completed request
    auto resources = event_capture.Resources();
    REQUIRE(resources.size() == 1);
    const auto& event = resources.front();
    REQUIRE(event["type"] == "resource");

    // And the event timestamp reflects the time of our StartResource call
    REQUIRE(event["date"] == 1700000000000);

    // And the event reflects the duration of our request along with its method, URL,
    // and response details
    REQUIRE(event["resource"]["duration"] == 5000000);
    REQUIRE(event["resource"]["method"] == "GET");
    REQUIRE(event["resource"]["url"] == "https://www.example.com/api/foo");
    REQUIRE(event["resource"]["status_code"] == 403);
    REQUIRE(event["resource"]["size"] == 16);
    REQUIRE(event["resource"]["type"] == "other");
  }

  SECTION(
      "M close and send error event W StopResource is processed with matching key, "
      "error details"
  ) {
    // Given a resource that's been active for 5ms
    clock.TickMilliseconds(5);

    // When we process a StopResource command that targets our resource scope and that
    // includes valid error details
    const auto result = scope.Process(
        RumCommand::StopResource(
            GetBaseParams(),
            "my-resource-key",
            RumResponseDetails{100},
            RumErrorDetails{"oh no", "BadException", "stack\ntrace\n"}
        )
    );

    // Then our resource scope is closed
    REQUIRE(result == RumScopeResult::Close);

    // And a single 'error' event is generated to describe the failed request
    auto errors = event_capture.Errors();
    REQUIRE(errors.size() == 1);
    const auto& event = errors.front();
    REQUIRE(event["type"] == "error");

    // And the event timestamp reflects the time of the error, i.e. when the resource
    // was _stopped_
    REQUIRE(event["date"] == 1700000000005);

    // And the event reflects the details of our error
    REQUIRE(event["error"]["message"] == "oh no");
    REQUIRE(event["error"]["type"] == "BadException");
    REQUIRE(event["error"]["stack"] == "stack\ntrace\n");
    REQUIRE(event["error"]["source"] == "network");

    // And the event reflects the basic details of our request
    REQUIRE(event["error"]["resource"]["method"] == "GET");
    REQUIRE(event["error"]["resource"]["url"] == "https://www.example.com/api/foo");
    REQUIRE(event["error"]["resource"]["status_code"] == 100);
  }
}
