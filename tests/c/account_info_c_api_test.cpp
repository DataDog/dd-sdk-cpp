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

#include "support/catch.hpp"
#include "support/core.hpp"
#include "support/diagnostics.hpp"
#include "support/json_validation.hpp"

// This file contains tests for the AccountInfo C API:
//
// - dd_core_set_account_info()
// - dd_core_clear_account_info()
// - dd_core_add_account_extra_info()
//
// The AccountInfo data modified by these functions is used across multiple SDK
// features. These tests validate that given a particular set of AccountInfo API calls,
// all such features produce the expected "account" values in all JSON events.

// Catch2 tags for all features that are exercised by these tests
#define FEATURE_TAGS "[logging][rum]"

TEST_CASE("dd_core account info", "[unit][core]" FEATURE_TAGS "[c-api]") {
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
  dd_rum_t* rum = dd_rum_init(core, &rum_config);

  // And a helper function that will cause each feature to produce one or more event
  // payloads, then resolve those event payloads, parse the value assigned to "account",
  // and return a nlohmann::json value representing the account info encoded in each
  // feature's event(s)
  struct AccountValues {
    nlohmann::json logging;  // 'account' payload from the single log event produced
    nlohmann::json rum;      // identical 'account' payload from all RUM events produced

    /** Asserts that all features had identical 'account' values, then returns one. */
    nlohmann::json require_identical() {
      REQUIRE(logging == rum);
      return rum;
    }
  };
  auto generate_events_and_stop_core = [&]() -> AccountValues {
    // Each test case should call dd_core_start(); we assume it's already started

    // Produce a single log event
    dd_logger_info(logger, "hello", nullptr, nullptr);

    // Produce a series of RUM events to verify that account info is faithfully conveyed
    // across all event types
    auto rum_event_types_tested =
        std::unordered_set<std::string>{"view", "action", "resource", "vital", "error"};
    dd_rum_start_view(rum, "view", nullptr);
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

    // Logging should have produced exactly one event, and its account info, if present,
    // should be stored as a property called "account"
    REQUIRE(log_events.size() == 1);
    auto log_event = log_events[0];
    auto log_account_info = log_event.value("account", nlohmann::json{});

    // RUM should have produced at least one event, and its account info should be
    // stored as "account" also
    REQUIRE(rum_events.size() > 0);
    auto rum_account_info = rum_events[0].value("account", nlohmann::json{});

    // And we should have at least one event for every RUM event type we expected to
    // see, and all those events should carry the exact same set of account info
    std::unordered_set<std::string> rum_event_types;
    for (const auto& rum_event : rum_events) {
      auto event_type = rum_event.value("type", nlohmann::json{});
      REQUIRE(event_type.is_string());
      rum_event_types.insert(event_type);
      REQUIRE(rum_event.value("account", nlohmann::json{}) == rum_account_info);
    }
    REQUIRE(rum_event_types == rum_event_types_tested);

    return AccountValues{log_account_info, rum_account_info};
  };

  // It shouldn't matter whether we supply account info before or after the SDK is
  // started: use a couple of helpers to run each test case in two scenarios
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

  SECTION(
      "M include no AccountInfo in events W dd_core_set_account_info is never called"
  ) {
    dd_core_start(core);
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical().is_null());
  }

  SECTION("M include AccountInfo in events W dd_core_set_account_info is called") {
    early_core_start();
    dd_core_set_account_info(core, "acct-123", "Bits", nullptr);
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(
        account_values.require_identical() ==
        nlohmann::json{{"id", "acct-123"}, {"name", "Bits"}}
    );
  }

  SECTION(
      "M merge extra attributes W dd_core_set_account_info includes extra attributes"
  ) {
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(1);
    dd_attribute_t str_gold = dd_attribute_string("gold");
    dd_attribute_object_property_set(&extra, "tier", &str_gold);
    dd_core_set_account_info(core, "acct-123", "Bits", &extra);
    dd_attribute_free(&str_gold);
    dd_attribute_free(&extra);
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(
        account_values.require_identical() ==
        nlohmann::json{{"id", "acct-123"}, {"name", "Bits"}, {"tier", "gold"}}
    );
  }

  SECTION(
      "M not permit extra attributes to override id and name W extra attributes have "
      "conflicting property names"
  ) {
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(3);
    dd_attribute_t str_foo = dd_attribute_string("foo");
    dd_attribute_t str_bar = dd_attribute_string("bar");
    dd_attribute_t str_just_fine = dd_attribute_string("just-fine");
    dd_attribute_object_property_set(&extra, "id", &str_foo);
    dd_attribute_object_property_set(&extra, "name", &str_bar);
    dd_attribute_object_property_set(&extra, "not-reserved", &str_just_fine);
    dd_core_set_account_info(core, "acct-123", "Bits", &extra);
    dd_attribute_free(&str_foo);
    dd_attribute_free(&str_bar);
    dd_attribute_free(&str_just_fine);
    dd_attribute_free(&extra);
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(
        account_values.require_identical() ==
        nlohmann::json{
            {"id", "acct-123"}, {"name", "Bits"}, {"not-reserved", "just-fine"}
        }
    );
  }

  SECTION("M merge extra attributes W dd_core_add_account_extra_info is called") {
    dd_attribute_t extra = dd_attribute_object(2);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_t int_200 = dd_attribute_int(200);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_attribute_object_property_set(&extra, "bar", &int_200);
    dd_core_set_account_info(core, "acct-123", "Bits", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&int_200);
    dd_attribute_free(&extra);

    early_core_start();
    dd_attribute_t extra_extra = dd_attribute_object(2);
    dd_attribute_t str_hello = dd_attribute_string("hello");
    dd_attribute_t str_world = dd_attribute_string("world");
    dd_attribute_object_property_set(&extra_extra, "bar", &str_hello);
    dd_attribute_object_property_set(&extra_extra, "baz", &str_world);
    dd_attribute_free(&str_hello);
    dd_attribute_free(&str_world);
    dd_core_add_account_extra_info(core, &extra_extra);
    dd_attribute_free(&extra_extra);
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(
        account_values.require_identical() == nlohmann::json{
                                                  {"id", "acct-123"},
                                                  {"name", "Bits"},
                                                  {"foo", 100},
                                                  {"bar", "hello"},
                                                  {"baz", "world"}
                                              }
    );
  }

  SECTION(
      "M set extra attributes W dd_core_add_account_extra_info is called without a "
      "prior call to dd_core_set_account_info"
  ) {
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(2);
    dd_attribute_t str_hello = dd_attribute_string("hello");
    dd_attribute_t str_world = dd_attribute_string("world");
    dd_attribute_object_property_set(&extra, "bar", &str_hello);
    dd_attribute_object_property_set(&extra, "baz", &str_world);
    dd_attribute_free(&str_hello);
    dd_attribute_free(&str_world);
    dd_core_add_account_extra_info(core, &extra);
    dd_attribute_free(&extra);
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    // Log events omit empty id; RUM events always include id per schema
    REQUIRE(
        account_values.logging == nlohmann::json{{"bar", "hello"}, {"baz", "world"}}
    );
    REQUIRE(
        account_values.rum ==
        nlohmann::json{{"bar", "hello"}, {"baz", "world"}, {"id", ""}}
    );
  }

  SECTION(
      "M ignore extra attributes W dd_core_add_account_extra_info is called with an "
      "empty or non-object value"
  ) {
    dd_attribute_t extra = dd_attribute_object(2);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_t int_200 = dd_attribute_int(200);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_attribute_object_property_set(&extra, "bar", &int_200);
    dd_core_set_account_info(core, "acct-123", "Bits", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&int_200);
    dd_attribute_free(&extra);

    early_core_start();
    SECTION("{empty object}") {
      dd_attribute_t attr = dd_attribute_object(0);
      dd_core_add_account_extra_info(core, &attr);
      dd_attribute_free(&attr);
    }
    SECTION("{non-object attribute}") {
      dd_attribute_t attr = dd_attribute_int(10000);
      dd_core_add_account_extra_info(core, &attr);
      dd_attribute_free(&attr);
    }
    SECTION("{null attribute}") {
      dd_attribute_t attr = dd_attribute_null();
      dd_core_add_account_extra_info(core, &attr);
      dd_attribute_free(&attr);
    }
    SECTION("{nullptr}") { dd_core_add_account_extra_info(core, nullptr); }
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(
        account_values.require_identical() ==
        nlohmann::json{{"id", "acct-123"}, {"name", "Bits"}, {"foo", 100}, {"bar", 200}}
    );
  }

  SECTION("M omit id W dd_core_set_account_info supplies no id") {
    auto no_value = GENERATE("", nullptr);

    early_core_start();
    dd_core_set_account_info(core, no_value, "Bits", nullptr);
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    // Log events omit empty id; RUM events always include id per schema
    REQUIRE(account_values.logging == nlohmann::json{{"name", "Bits"}});
    REQUIRE(account_values.rum == nlohmann::json{{"id", ""}, {"name", "Bits"}});
  }

  SECTION("M omit name W dd_core_set_account_info supplies no name") {
    auto no_value = GENERATE("", nullptr);

    early_core_start();
    dd_core_set_account_info(core, "acct-123", no_value, nullptr);
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical() == nlohmann::json{{"id", "acct-123"}});
  }

  SECTION(
      "M entirely replace all account info W dd_core_set_account_info is called again"
  ) {
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(1);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_core_set_account_info(core, "acct-123", "Bits", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&extra);

    late_core_start();
    dd_core_set_account_info(core, "acct-234", "", nullptr);
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical() == nlohmann::json{{"id", "acct-234"}});
  }

  SECTION(
      "M clear all account info W dd_core_set_account_info is called again with no "
      "values"
  ) {
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(1);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_core_set_account_info(core, "acct-123", "Bits", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&extra);

    late_core_start();
    dd_core_set_account_info(core, "", "", nullptr);
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical().is_null());
  }

  SECTION("M clear all account info W dd_core_clear_account_info is called") {
    early_core_start();
    dd_attribute_t extra = dd_attribute_object(1);
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_object_property_set(&extra, "foo", &int_100);
    dd_core_set_account_info(core, "acct-123", "Bits", &extra);
    dd_attribute_free(&int_100);
    dd_attribute_free(&extra);

    late_core_start();
    dd_core_clear_account_info(core);
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical().is_null());
  }

  // Cleanup
  dd_rum_destroy(rum);
  dd_logger_destroy(logger);
  dd_logging_destroy(logging);
  dd_core_destroy(core);
}
