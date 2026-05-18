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

// This file contains tests for the AccountInfo C++ API:
//
// - Core::SetAccountInfo()
// - Core::ClearAccountInfo()
// - Core::AddAccountExtraInfo()
//
// The AccountInfo data modified by these functions is used across multiple SDK
// features. These tests validate that given a particular set of AccountInfo API calls,
// all such features produce the expected "account" values in all JSON events.

// Catch2 tags for all features that are exercised by these tests
#define FEATURE_TAGS "[logging][rum]"

using namespace datadog;

TEST_CASE("Core account info", "[unit][core]" FEATURE_TAGS "[cpp-api]") {
  // Given a started core
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  auto core = CoreTestHarness::WrapForCpp(test);

  // With an active logging API
  auto logging = Logging::Register(core);
  auto logger = logging->CreateLogger();

  // And an active RUM API
  auto rum = Rum::Register(core, RumConfig("a991ca10-4004-4004-4004-beefbeefbeef"));

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
    // Each test case should call Core::Start(); we assume it's already started

    // Produce a single log event
    logger->Info("hello");

    // Produce a series of RUM events to verify that account info is faithfully conveyed
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
      core->Start();
    }
  };
  auto late_core_start = [&]() {
    if (!start_early) {
      core->Start();
    }
  };

  SECTION("M include no AccountInfo in events W Core::SetAccountInfo is never called") {
    core->Start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical().is_null());
  }

  SECTION("M include AccountInfo in events W Core::SetAccountInfo is called") {
    early_core_start();
    core->SetAccountInfo("acct-123", "Bits");
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(
        account_values.require_identical() ==
        nlohmann::json{{"id", "acct-123"}, {"name", "Bits"}}
    );
  }

  SECTION("M merge extra attributes W Core::SetAccountInfo includes extra attributes") {
    early_core_start();
    Attribute extra = Attribute::Object(1);
    extra.SetObjectProperty("tier", Attribute::String("gold"));
    core->SetAccountInfo("acct-123", "Bits", extra);
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
    Attribute extra = Attribute::Object(3);
    extra.SetObjectProperty("id", Attribute::String("foo"));
    extra.SetObjectProperty("name", Attribute::String("bar"));
    extra.SetObjectProperty("not-reserved", Attribute::String("just-fine"));
    core->SetAccountInfo("acct-123", "Bits", extra);
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(
        account_values.require_identical() ==
        nlohmann::json{
            {"id", "acct-123"}, {"name", "Bits"}, {"not-reserved", "just-fine"}
        }
    );
  }

  SECTION("M merge extra attributes W Core::AddAccountExtraInfo is called") {
    Attribute extra = Attribute::Object(2);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    extra.SetObjectProperty("bar", Attribute::Int(200));
    core->SetAccountInfo("acct-123", "Bits", extra);

    early_core_start();
    Attribute extra_extra = Attribute::Object(2);
    extra_extra.SetObjectProperty("bar", Attribute::String("hello"));
    extra_extra.SetObjectProperty("baz", Attribute::String("world"));
    core->AddAccountExtraInfo(extra_extra);
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
      "M set extra attributes W Core::AddAccountExtraInfo is called without a prior "
      "call to Core::SetAccountInfo"
  ) {
    early_core_start();
    Attribute extra = Attribute::Object(2);
    extra.SetObjectProperty("bar", Attribute::String("hello"));
    extra.SetObjectProperty("baz", Attribute::String("world"));
    core->AddAccountExtraInfo(extra);
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(
        account_values.require_identical() ==
        nlohmann::json{{"bar", "hello"}, {"baz", "world"}}
    );
  }

  SECTION(
      "M ignore extra attributes W Core::AddAccountExtraInfo is called with an empty "
      "or non-object value"
  ) {
    Attribute extra = Attribute::Object(2);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    extra.SetObjectProperty("bar", Attribute::Int(200));
    core->SetAccountInfo("acct-123", "Bits", extra);

    early_core_start();
    SECTION("{empty object}") { core->AddAccountExtraInfo(Attribute::Object(0)); }
    SECTION("{non-object attribute}") {
      core->AddAccountExtraInfo(Attribute::Int(10000));
    }
    SECTION("{null attribute}") { core->AddAccountExtraInfo(Attribute{}); }
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(
        account_values.require_identical() ==
        nlohmann::json{{"id", "acct-123"}, {"name", "Bits"}, {"foo", 100}, {"bar", 200}}
    );
  }

  SECTION("M omit id W Core::SetAccountInfo supplies no id") {
    early_core_start();
    core->SetAccountInfo("", "Bits");
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical() == nlohmann::json{{"name", "Bits"}});
  }

  SECTION("M omit name W Core::SetAccountInfo supplies no name") {
    early_core_start();
    core->SetAccountInfo("acct-123", "");
    late_core_start();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical() == nlohmann::json{{"id", "acct-123"}});
  }

  SECTION(
      "M entirely replace all account info W Core::SetAccountInfo is called again"
  ) {
    early_core_start();
    Attribute extra = Attribute::Object(1);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    core->SetAccountInfo("acct-123", "Bits", extra);

    late_core_start();
    core->SetAccountInfo("acct-234", "");
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical() == nlohmann::json{{"id", "acct-234"}});
  }

  SECTION(
      "M clear all account info W Core::SetAccountInfo is called again with no values"
  ) {
    early_core_start();
    Attribute extra = Attribute::Object(1);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    core->SetAccountInfo("acct-123", "Bits", extra);

    late_core_start();
    core->SetAccountInfo("");
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical().is_null());
  }

  SECTION("M clear all account info W Core::ClearAccountInfo is called") {
    early_core_start();
    Attribute extra = Attribute::Object(1);
    extra.SetObjectProperty("foo", Attribute::Int(100));
    core->SetAccountInfo("acct-123", "Bits", extra);

    late_core_start();
    core->ClearAccountInfo();
    auto account_values = generate_events_and_stop_core();

    REQUIRE(account_values.require_identical().is_null());
  }
}
