// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_set>

#include "datadog/core.hpp"
#include "datadog/logging.hpp"
#include "datadog/rum.hpp"

#include "support/catch.hpp"
#include "support/core.hpp"
#include "support/diagnostics.hpp"
#include "support/json_validation.hpp"

// This file contains tests for the UserInfo C++ API:
//
// - Core::SetUserInfo()
// - Core::ClearUserInfo()
// - Core::AddUserExtraInfo()
//
// The UserInfo data modified by these functions is used across multiple SDK features.
// These tests validate that the given a particular set of UserInfo API calls, all such
// features produce the expected "usr" values in all JSON events.

// Catch2 tags for all features that are exercised by these tests
#define FEATURE_TAGS "[logging][rum]"

using namespace datadog;

TEST_CASE("Core user info", "[unit][core]" FEATURE_TAGS "[cpp-api]") {
  // Given a started core
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  auto core = CoreTestHarness::WrapForCpp(test);

  // With an active logging API
  auto logging = Logging::Register(core);
  auto logger = logging->CreateLogger();

  // And an active RUM API
  auto rum = Rum::Register(core, RumConfig("a991ca10-4004-4004-4004-beefbeefbeef"));

  // And a helper function that will cause each feature to produce a one or more event
  // payloads, then resolve those event payloads, parse the value assigned to "usr" (or
  // "meta.usr", etc.), and return a nlohmann::json value representing the user info
  // encoded in each feature's event(s)
  struct UserValues {
    nlohmann::json logging;  // 'usr' payload from the single log event produced
    nlohmann::json rum;      // identical 'usr' payload from all RUM events produced

    /** Asserts that all features had identical 'usr' values, then returns one. */
    nlohmann::json require_identical() {
      REQUIRE(logging == rum);
      return rum;
    }
  };
  auto generate_events_and_stop_core = [&]() -> UserValues {
    // Each test case should call Core::Start(); we assume it's already started

    // Produce a single log event
    logger->Info("hello");

    // Produce a series of RUM events to verify that user info is faithfully conveyed
    // across all event types
    auto rum_event_types_tested =
        std::unordered_set<std::string>{"view", "action", "resource", "vital", "error"};
    rum->StartView("view");
    rum->AddAction(RumActionType::Custom, "action");
    rum->StartResource("resource", RumResourceMethod::Get, "/foo");
    rum->StopResource("resource", 200, 0, RumResourceType::Other);
    rum->StartOperation("operation");
    rum->SucceedOperation("operation");
    rum->AddError(RumErrorSource::Custom, "err!", "err");

    // Stop the core to flush all events to storage, then flush all uploads
    // synchronously thanks to flush_http_requests in CoreTestHarness
    core->Stop();

    // Extract the raw JSON objects corresponding to each feature's events
    auto log_events = MergeJsonArrays(test.client.requests, "/api/v2/logs");
    auto rum_events = MergeJsonArrays(test.client.requests, "/api/v2/rum");

    // Logging should have produced exactly one event, and its user info, if present,
    // should be stored as a property called "usr"
    REQUIRE(log_events.size() == 1);
    auto log_event = log_events[0];
    auto log_user_info = log_event.value("usr", nlohmann::json{});

    // Rum should have produced at least one event, and its user info should be stored
    // as "usr" also
    REQUIRE(rum_events.size() > 0);
    auto rum_user_info = rum_events[0].value("usr", nlohmann::json{});

    // And we should have at least one event for every RUM event type we expected to
    // see, and all those events should carry the exact same set of user info
    std::unordered_set<std::string> rum_event_types;
    for (const auto& rum_event : rum_events) {
      auto event_type = rum_event.value("type", nlohmann::json{});
      REQUIRE(event_type.is_string());
      rum_event_types.insert(event_type);
      REQUIRE(rum_event.value("usr", nlohmann::json{}) == rum_user_info);
    }
    REQUIRE(rum_event_types == rum_event_types_tested);

    // Now that we've pulled user info from all features' event types (defaulting to a
    // JSON null if not set), ensuring that they're consistent across all events for
    // features that have generated multiple events: return representative values for
    // each feature
    return UserValues{log_user_info, rum_user_info};
  };

  // It shouldn't matter whether we supply user info before or after the SDK is started:
  // use a couple of helpers to run each test case in two scenarios with different Core
  // start timing
  auto start_early = GENERATE(false, true);
  auto early_core_start = [&]() {
    if (start_early) {
      core->Start();
    }
  };
  auto late_core_start = [&]() {
    if (!start_early) {
      core->Start();
    }
  };

  // When we produce these events after various UserInfo API calls, Then the resulting
  // event payloads produced by each feature contain the expected set of "usr" data

  SECTION("M include no UserInfo in events W Core::SetUserInfo is never called") {
    // When we run the SDK and generate events without ever supplying user info
    core->Start();
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events with no "usr" value present
    REQUIRE(user_values.require_identical().is_null());
  }

  SECTION("M include UserInfo in events W Core::SetUserInfo is called") {
    // When we supply basic user info, regardless of whether it's before or after start
    early_core_start();
    core->SetUserInfo("user-123", "Jane Doe", "jane@example.com");
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events with valid, identical "usr" objects that match
    // the user details we supplied
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{
            {"id", "user-123"}, {"name", "Jane Doe"}, {"email", "jane@example.com"}
        }
    );
  }

  SECTION("M merge extra attributes W Core::SetUserInfo includes extra attributes") {
    // When we supply basic user info, including an extra attribute
    early_core_start();
    Attribute extra = Attribute::Object(1);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    core->SetUserInfo("user-123", "Jane Doe", "jane@example.com", extra);
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events with valid, identical "usr" objects that match
    // the user details we supplied
    REQUIRE(
        user_values.require_identical() == nlohmann::json{
                                               {"id", "user-123"},
                                               {"name", "Jane Doe"},
                                               {"email", "jane@example.com"},
                                               {"foo", 100}
                                           }
    );
  }

  SECTION(
      "M not permit extra user attributes to override id, name, and email W extra "
      "attributes have conflicting property names"
  ) {
    // When we supply user info, but we also supply a set of extra attributes that use
    // the reserved JSON property names "id", "name", and "email"
    early_core_start();
    Attribute extra = Attribute::Object(4);
    extra.SetObjectProperty("id", Attribute::String("foo"));
    extra.SetObjectProperty("name", Attribute::String("bar"));
    extra.SetObjectProperty("email", Attribute::String("baz"));
    extra.SetObjectProperty("not-reserved", Attribute::String("just-fine"));
    core->SetUserInfo("user-123", "Jane Doe", "jane@example.com", extra);
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then the resulting "usr" events preserve the canonical 'id', 'name', and 'email'
    // values that were provided as positional arguments; they do not allow extra
    // attributes to shadow those values
    REQUIRE(
        user_values.require_identical() == nlohmann::json{
                                               {"id", "user-123"},
                                               {"name", "Jane Doe"},
                                               {"email", "jane@example.com"},
                                               {"not-reserved", "just-fine"}
                                           }
    );
  }

  SECTION("M merge extra attributes W Core::AddUserExtraInfo is called") {
    // When we supply basic user info, including extra attributes "foo" and "bar"
    Attribute extra = Attribute::Object(2);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    extra.SetObjectProperty("bar", Attribute::Int(200));
    core->SetUserInfo("user-123", "Jane Doe", "jane@example.com", extra);

    // And then we subsequently add two more extra attribute values, "bar" and "baz"
    early_core_start();
    Attribute extra_extra = Attribute::Object(2);
    extra_extra.SetObjectProperty("bar", Attribute::String("hello"));
    extra_extra.SetObjectProperty("baz", Attribute::String("world"));
    core->AddUserExtraInfo(extra_extra);
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all "usr" objects contain the final, merged set of custom attribute values,
    // with the original "foo" and the updated "bar"
    REQUIRE(
        user_values.require_identical() == nlohmann::json{
                                               {"id", "user-123"},
                                               {"name", "Jane Doe"},
                                               {"email", "jane@example.com"},
                                               {"foo", 100},
                                               {"bar", "hello"},
                                               {"baz", "world"}
                                           }
    );
  }

  SECTION(
      "M set extra attributes W Core::AddUserExtraInfo is called without a prior "
      "call to Core::SetUserInfo"
  ) {
    // When we add extra attributes to an SDK instance which has no preexisting user
    // info
    early_core_start();
    Attribute extra = Attribute::Object(2);
    extra.SetObjectProperty("bar", Attribute::String("hello"));
    extra.SetObjectProperty("baz", Attribute::String("world"));
    core->AddUserExtraInfo(extra);
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all events have a "usr" field that contains those extra attributes by
    // themselves, without any id/name/email
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{{"bar", "hello"}, {"baz", "world"}}
    );
  }

  SECTION(
      "M ignore extra attributes W Core::AddUserExtraInfo is called with an empty "
      "or non-object value"
  ) {
    // When we supply basic user info, including extra attributes "foo" and "bar"
    Attribute extra = Attribute::Object(2);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    extra.SetObjectProperty("bar", Attribute::Int(200));
    core->SetUserInfo("user-123", "Jane Doe", "jane@example.com", extra);

    // And then we subsequently attempt to add extra attribute values, but we pass a
    // value that is not a valid object with 1 or more properties
    early_core_start();
    SECTION("{empty object}") { core->AddUserExtraInfo(Attribute::Object(0)); }
    SECTION("{non-object attribute}") { core->AddUserExtraInfo(Attribute::Int(10000)); }
    SECTION("{null attribute}") { core->AddUserExtraInfo(Attribute{}); }
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all "usr" objects contain the original set of extra attribute values,
    // entirely unchanged
    REQUIRE(
        user_values.require_identical() == nlohmann::json{
                                               {"id", "user-123"},
                                               {"name", "Jane Doe"},
                                               {"email", "jane@example.com"},
                                               {"foo", 100},
                                               {"bar", 200}
                                           }
    );
  }

  SECTION("M omit id W Core::SetUserInfo supplies no id") {
    // When we supply basic user info without supplying an id
    early_core_start();
    core->SetUserInfo("", "Jane Doe", "jane@example.com");
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events that include our user name and email, but no id
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{{"name", "Jane Doe"}, {"email", "jane@example.com"}}
    );
  }

  SECTION("M omit name W Core::SetUserInfo supplies no name") {
    // When we supply basic user info without supplying a name
    early_core_start();
    core->SetUserInfo("user-123", "", "jane@example.com");
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events that include our user id and email, but no name
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{{"id", "user-123"}, {"email", "jane@example.com"}}
    );
  }

  SECTION("M omit email W Core::SetUserInfo supplies no email") {
    // When we supply basic user info without supplying an email
    early_core_start();
    core->SetUserInfo("user-123", "Jane Doe", "");
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events that include our user id and name, but no email
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{{"id", "user-123"}, {"name", "Jane Doe"}}
    );
  }

  SECTION("M entirely replace all user info W Core::SetUserInfo is called again") {
    // When we supply user info
    early_core_start();
    Attribute extra = Attribute::Object(1);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    core->SetUserInfo("user-123", "Jane Doe", "jane@example.com", extra);

    // And then subsequently supply an entirely different set of user attributes, with
    // some fields omitted
    late_core_start();
    core->SetUserInfo("user-234", "John Public", "");
    auto user_values = generate_events_and_stop_core();

    // Then all "usr" objects match the user details we supplied in the _second_ call,
    // with no leftover data from the original user info
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{{"id", "user-234"}, {"name", "John Public"}}
    );
  }

  SECTION("M clear all user info W Core::SetUserInfo is called again with no values") {
    // When we supply user info
    early_core_start();
    Attribute extra = Attribute::Object(1);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    core->SetUserInfo("user-123", "Jane Doe", "jane@example.com", extra);

    // And then subsequently supply an entirely different set of user attributes, all of
    // which are empty
    late_core_start();
    core->SetUserInfo("");
    auto user_values = generate_events_and_stop_core();

    // Then all features revert to producing events with no "usr" data whatsoever
    REQUIRE(user_values.require_identical().is_null());
  }

  SECTION("M clear all user info W Core::ClearUserInfo is called") {
    // When we supply user info
    early_core_start();
    Attribute extra = Attribute::Object(1);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    core->SetUserInfo("user-123", "Jane Doe", "jane@example.com", extra);

    // And then we explicitly clear user info
    late_core_start();
    core->ClearUserInfo();
    auto user_values = generate_events_and_stop_core();

    // Then all features revert to producing events with no "usr" data whatsoever
    REQUIRE(user_values.require_identical().is_null());
  }
}
