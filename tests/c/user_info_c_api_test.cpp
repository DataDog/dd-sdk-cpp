// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_set>

#include "datadog/core.h"
#include "datadog/logging.h"
#include "datadog/rum.h"
#include "datadog/uuid.hpp"

#include "support/catch.hpp"
#include "support/core.hpp"
#include "support/diagnostics.hpp"
#include "support/json_validation.hpp"

// This file contains tests for the UserInfo C API:
//
// - dd_core_set_user_info()
// - dd_core_clear_user_info()
// - dd_core_add_user_extra_info()
//
// The UserInfo data modified by these functions is used across multiple SDK features.
// These tests validate that the given a particular set of UserInfo API calls, all such
// features produce the expected "usr" values in all JSON events.

// Catch2 tags for all features that are exercised by these tests
#define FEATURE_TAGS "[logging][rum]"

TEST_CASE("dd_core user info", "[unit][core]" FEATURE_TAGS "[c-api]") {
  // Given a started core
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  dd_core_t* core = CoreTestHarness::WrapForC(test);

  // With an active logging API
  dd_logging_t* logging = dd_logging_init(core);
  dd_logger_t* logger = dd_logger_create(logging, nullptr);

  // And an active RUM API
  dd_rum_config_t rum_config;
  dd_rum_config_init(&rum_config, "a991ca10-4004-4004-4004-beefbeefbeef");
  // Disable anonymous id tracking: these tests are specifically about UserInfo and
  // don't expect a usr.anonymous_id value to show up alongside it.
  rum_config.track_anonymous_user = false;
  dd_rum_t* rum = dd_rum_init(core, &rum_config);

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
    // Each test case should call dd_core_start(); we assume it's already started

    // Produce a single log event
    dd_logger_info(logger, "hello", nullptr, nullptr);

    // Produce a series of RUM events to verify that user info is faithfully conveyed
    // across all event types
    auto rum_event_types_tested =
        std::unordered_set<std::string>{"view", "action", "resource", "vital", "error"};
    dd_rum_start_view(rum, "view", nullptr, nullptr);
    dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "action", nullptr);
    dd_rum_start_resource(rum, "resource", DD_RUM_RESOURCE_METHOD_GET, "/foo", nullptr);
    dd_rum_stop_resource(rum, "resource", 200, 0, DD_RUM_RESOURCE_TYPE_OTHER, nullptr);
    dd_rum_start_operation(rum, "operation", nullptr, nullptr);
    dd_rum_succeed_operation(rum, "operation", nullptr, nullptr);
    dd_rum_add_error(rum, DD_RUM_ERROR_SOURCE_CUSTOM, "err!", "err", nullptr, nullptr);

    // Stop the core to flush all events to storage, then flush all uploads
    // synchronously thanks to flush_http_requests in CoreTestHarness
    dd_core_stop(core);

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
      dd_core_start(core);
    }
  };
  auto late_core_start = [&]() {
    if (!start_early) {
      dd_core_start(core);
    }
  };

  // When we produce these events after various UserInfo API calls, Then the resulting
  // event payloads produced by each feature contain the expected set of "usr" data

  SECTION("M include no UserInfo in events W dd_core_set_user_info is never called") {
    // When we run the SDK and generate events without ever supplying user info
    dd_core_start(core);
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events with no "usr" value present
    REQUIRE(user_values.require_identical().is_null());
  }

  SECTION("M include UserInfo in events W dd_core_set_user_info is called") {
    // When we supply basic user info, regardless of whether it's before or after start
    early_core_start();
    dd_core_set_user_info(core, "user-123", "Jane Doe", "jane@example.com", nullptr);
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

  SECTION(
      "M merge extra attributes W dd_core_set_user_info includes extra attributes"
  ) {
    // When we supply basic user info, including an extra attribute
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(1);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_core_set_user_info(core, "user-123", "Jane Doe", "jane@example.com", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&extra);
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
    dd_attribute_t extra = dd_attribute_object(4);
    dd_attribute_t str_foo = dd_attribute_string("foo");
    dd_attribute_t str_bar = dd_attribute_string("bar");
    dd_attribute_t str_baz = dd_attribute_string("baz");
    dd_attribute_t str_just_fine = dd_attribute_string("just-fine");
    dd_attribute_object_property_set(&extra, "id", &str_foo);
    dd_attribute_object_property_set(&extra, "name", &str_bar);
    dd_attribute_object_property_set(&extra, "email", &str_baz);
    dd_attribute_object_property_set(&extra, "not-reserved", &str_just_fine);
    dd_core_set_user_info(core, "user-123", "Jane Doe", "jane@example.com", &extra);
    dd_attribute_free(&str_foo);
    dd_attribute_free(&str_bar);
    dd_attribute_free(&str_baz);
    dd_attribute_free(&str_just_fine);
    dd_attribute_free(&extra);
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

  SECTION("M merge extra attributes W dd_core_add_user_extra_info is called") {
    // When we supply basic user info, including extra attributes "foo" and "bar"
    dd_attribute_t extra = dd_attribute_object(2);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_t int_200 = dd_attribute_int(200);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_attribute_object_property_set(&extra, "bar", &int_200);
    dd_core_set_user_info(core, "user-123", "Jane Doe", "jane@example.com", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&int_200);
    dd_attribute_free(&extra);

    // And then we subsequently add two more extra attribute values, "bar" and "baz"
    early_core_start();
    dd_attribute_t extra_extra = dd_attribute_object(2);
    dd_attribute_t str_hello = dd_attribute_string("hello");
    dd_attribute_t str_world = dd_attribute_string("world");
    dd_attribute_object_property_set(&extra_extra, "bar", &str_hello);
    dd_attribute_object_property_set(&extra_extra, "baz", &str_world);
    dd_attribute_free(&str_hello);
    dd_attribute_free(&str_world);
    dd_core_add_user_extra_info(core, &extra_extra);
    dd_attribute_free(&extra_extra);
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
      "M set extra attributes W dd_core_add_user_extra_info is called without a prior "
      "call to dd_core_set_user_info"
  ) {
    // When we add extra attributes to an SDK instance which has no preexisting user
    // info
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(2);
    dd_attribute_t str_hello = dd_attribute_string("hello");
    dd_attribute_t str_world = dd_attribute_string("world");
    dd_attribute_object_property_set(&extra, "bar", &str_hello);
    dd_attribute_object_property_set(&extra, "baz", &str_world);
    dd_attribute_free(&str_hello);
    dd_attribute_free(&str_world);
    dd_core_add_user_extra_info(core, &extra);
    dd_attribute_free(&extra);
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
      "M ignore extra attributes W dd_core_add_user_extra_info is called with an empty "
      "or non-object value"
  ) {
    // When we supply basic user info, including extra attributes "foo" and "bar"
    dd_attribute_t extra = dd_attribute_object(2);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_t int_200 = dd_attribute_int(200);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_attribute_object_property_set(&extra, "bar", &int_200);
    dd_core_set_user_info(core, "user-123", "Jane Doe", "jane@example.com", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&int_200);
    dd_attribute_free(&extra);

    // And then we subsequently attempt to add extra attribute values, but we pass a
    // value that is not a valid object with 1 or more properties
    early_core_start();
    SECTION("{empty object}") {
      dd_attribute_t attr = dd_attribute_object(0);
      dd_core_add_user_extra_info(core, &attr);
      dd_attribute_free(&attr);
    }
    SECTION("{non-object attribute}") {
      dd_attribute_t attr = dd_attribute_int(10000);
      dd_core_add_user_extra_info(core, &attr);
      dd_attribute_free(&attr);
    }
    SECTION("{null attribute}") {
      dd_attribute_t attr = dd_attribute_null();
      dd_core_add_user_extra_info(core, &attr);
      dd_attribute_free(&attr);
    }
    SECTION("{nullptr}") { dd_core_add_user_extra_info(core, nullptr); }
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

  SECTION("M omit id W dd_core_set_user_info supplies no id") {
    // Given either a value of empty string or NULL for user id
    auto no_value = GENERATE("", nullptr);

    // When we supply basic user info without supplying an id
    early_core_start();
    dd_core_set_user_info(core, no_value, "Jane Doe", "jane@example.com", nullptr);
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events that include our user name and email, but no id
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{{"name", "Jane Doe"}, {"email", "jane@example.com"}}
    );
  }

  SECTION("M omit name W dd_core_set_user_info supplies no name") {
    // Given either a value of empty string or NULL for user name
    auto no_value = GENERATE("", nullptr);

    // When we supply basic user info without supplying a name
    early_core_start();
    dd_core_set_user_info(core, "user-123", no_value, "jane@example.com", nullptr);
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events that include our user id and email, but no name
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{{"id", "user-123"}, {"email", "jane@example.com"}}
    );
  }

  SECTION("M omit email W dd_core_set_user_info supplies no email") {
    // Given either a value of empty string or NULL for user email
    auto no_value = GENERATE("", nullptr);

    // When we supply basic user info without supplying an email
    early_core_start();
    dd_core_set_user_info(core, "user-123", "Jane Doe", no_value, nullptr);
    late_core_start();
    auto user_values = generate_events_and_stop_core();

    // Then all features produce events that include our user id and name, but no email
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{{"id", "user-123"}, {"name", "Jane Doe"}}
    );
  }

  SECTION("M entirely replace all user info W dd_core_set_user_info is called again") {
    // When we supply user info
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(1);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_core_set_user_info(core, "user-123", "Jane Doe", "jane@example.com", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&extra);

    // And then subsequently supply an entirely different set of user attributes, with
    // some fields omitted
    late_core_start();
    dd_core_set_user_info(core, "user-234", "John Public", "", nullptr);
    auto user_values = generate_events_and_stop_core();

    // Then all "usr" objects match the user details we supplied in the _second_ call,
    // with no leftover data from the original user info
    REQUIRE(
        user_values.require_identical() ==
        nlohmann::json{{"id", "user-234"}, {"name", "John Public"}}
    );
  }

  SECTION(
      "M clear all user info W dd_core_set_user_info is called again with no values"
  ) {
    // When we supply user info
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(1);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_core_set_user_info(core, "user-123", "Jane Doe", "jane@example.com", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&extra);

    // And then subsequently supply an entirely different set of user attributes, all of
    // which are empty
    late_core_start();
    dd_core_set_user_info(core, "", "", "", nullptr);
    auto user_values = generate_events_and_stop_core();

    // Then all features revert to producing events with no "usr" data whatsoever
    REQUIRE(user_values.require_identical().is_null());
  }

  SECTION("M clear all user info W dd_core_clear_user_info is called") {
    // When we supply user info
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(1);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_core_set_user_info(core, "user-123", "Jane Doe", "jane@example.com", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&extra);

    // And then we explicitly clear user info
    late_core_start();
    dd_core_clear_user_info(core);
    auto user_values = generate_events_and_stop_core();

    // Then all features revert to producing events with no "usr" data whatsoever
    REQUIRE(user_values.require_identical().is_null());
  }

  // Cleanup
  dd_rum_destroy(rum);
  dd_logger_destroy(logger);
  dd_logging_destroy(logging);
  dd_core_destroy(core);
}

TEST_CASE(
    "dd_core anonymous_id in user info events", "[unit][core]" FEATURE_TAGS "[c-api]"
) {
  // Given a started core, with active Logging and RUM APIs
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  dd_core_t* core = CoreTestHarness::WrapForC(test);

  dd_logging_t* logging = dd_logging_init(core);
  dd_logger_t* logger = dd_logger_create(logging, nullptr);

  auto track_anonymous_user = GENERATE(true, false);
  dd_rum_config_t rum_config;
  dd_rum_config_init(&rum_config, "a991ca10-4004-4004-4004-beefbeefbeef");
  rum_config.track_anonymous_user = track_anonymous_user;
  dd_rum_t* rum = dd_rum_init(core, &rum_config);
  REQUIRE(dd_core_start(core));

  // When we produce a log event and a RUM event, then stop the core to flush everything
  dd_logger_info(logger, "hello", nullptr, nullptr);
  dd_rum_start_view(rum, "view", nullptr, nullptr);
  dd_core_stop(core);

  auto log_events = MergeJsonArrays(test.client.requests, "/api/v2/logs");
  auto rum_events = MergeJsonArrays(test.client.requests, "/api/v2/rum");
  REQUIRE(log_events.size() == 1);
  REQUIRE(!rum_events.empty());

  auto log_usr = log_events[0].value("usr", nlohmann::json{});
  auto rum_usr = rum_events[0].value("usr", nlohmann::json{});

  if (track_anonymous_user) {
    // Then both Logging and RUM events carry the same, valid anonymous_id
    REQUIRE(log_usr.contains("anonymous_id"));
    REQUIRE(rum_usr.contains("anonymous_id"));
    std::string log_id = log_usr["anonymous_id"].get<std::string>();
    std::string rum_id = rum_usr["anonymous_id"].get<std::string>();
    REQUIRE(datadog::UUID::Parse(log_id).has_value());
    REQUIRE(log_id == rum_id);
  } else {
    // Then, with no other user info supplied and track_anonymous_user disabled, neither
    // feature populates 'usr' at all
    REQUIRE(log_usr.is_null());
    REQUIRE(rum_usr.is_null());
  }

  // Cleanup
  dd_rum_destroy(rum);
  dd_logger_destroy(logger);
  dd_logging_destroy(logging);
  dd_core_destroy(core);
}

TEST_CASE(
    "dd_core anonymous_id is never exposed W track_anonymous_user is false",
    "[unit][core]" FEATURE_TAGS "[c-api]"
) {
  // Given a started core, with active Logging and RUM APIs, RUM configured with
  // track_anonymous_user explicitly disabled
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  dd_core_t* core = CoreTestHarness::WrapForC(test);

  dd_logging_t* logging = dd_logging_init(core);
  dd_logger_t* logger = dd_logger_create(logging, nullptr);

  dd_rum_config_t rum_config;
  dd_rum_config_init(&rum_config, "a991ca10-4004-4004-4004-beefbeefbeef");
  rum_config.track_anonymous_user = false;
  dd_rum_t* rum = dd_rum_init(core, &rum_config);
  REQUIRE(dd_core_start(core));

  // And real UserInfo supplied, so that 'usr' *is* populated on events for another
  // reason entirely, giving anonymous_id somewhere to leak into if the gating logic
  // were wrong
  dd_core_set_user_info(core, "user-123", "Jane Doe", "jane@example.com", nullptr);

  // When we produce a log event and a RUM event, then stop the core to flush everything
  dd_logger_info(logger, "hello", nullptr, nullptr);
  dd_rum_start_view(rum, "view", nullptr, nullptr);
  dd_core_stop(core);

  // Then Core::Init() still resolved and persisted an anonymous_id to disk: the
  // underlying value exists, it's simply never exposed
  REQUIRE(test.fs.IsFile("app/.datadog/.core/id"));

  // And both Logging and RUM events carry 'usr' (from the real UserInfo we set), but
  // neither one includes an 'anonymous_id' property
  auto log_events = MergeJsonArrays(test.client.requests, "/api/v2/logs");
  auto rum_events = MergeJsonArrays(test.client.requests, "/api/v2/rum");
  REQUIRE(log_events.size() == 1);
  REQUIRE(!rum_events.empty());

  auto log_usr = log_events[0].value("usr", nlohmann::json{});
  auto rum_usr = rum_events[0].value("usr", nlohmann::json{});
  REQUIRE(log_usr.contains("id"));
  REQUIRE(rum_usr.contains("id"));
  REQUIRE_FALSE(log_usr.contains("anonymous_id"));
  REQUIRE_FALSE(rum_usr.contains("anonymous_id"));

  // Cleanup
  dd_rum_destroy(rum);
  dd_logger_destroy(logger);
  dd_logging_destroy(logging);
  dd_core_destroy(core);
}
