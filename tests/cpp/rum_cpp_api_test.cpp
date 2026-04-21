// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>

#include "datadog/rum.hpp"

#include "support/core.hpp"
#include "support/json_serialization.hpp"
#include "support/json_validation.hpp"

using namespace datadog;

TEST_CASE("Rum null safety", "[unit][rum][cpp-api]") {
  SECTION("M safely do nothing W this wraps nullptr") {
    // Given both a valid Core interface that has no valid implementation pointer, as
    // well as a straight-up null pointer to a Core interface
    CoreConfig invalid_config("", "", "");
    invalid_config.SetDiagnosticHandler(nullptr);
    std::shared_ptr<Core> noop_core = Core::Create(invalid_config);
    std::shared_ptr<Core> null_core;
    std::vector<std::shared_ptr<Core>> cores = {noop_core, null_core};

    // And a valid RUM Config
    const datadog::RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");

    for (std::shared_ptr<Core>& core : cores) {
      // When we register the RUM feature on an invalid core
      auto rum = Rum::Register(core, config);

      // Then we get a valid object that handles all member function calls as a no-op
      REQUIRE(rum != nullptr);
      rum->AddAttribute("foo", Attribute::Int(100));
      rum->RemoveAttribute("foo");

      rum->StopSession();

      Attribute attributes = Attribute::Object(1);
      attributes.SetObjectProperty("bar", Attribute::Int(100));
      rum->StartView("foo", "My View", attributes);
      rum->AddViewAttribute("something", Attribute::Int(100));
      rum->RemoveViewAttribute("something");
      rum->StopView("foo", attributes);

      rum->AddAction(RumActionType::Click, "Button");
      rum->StartAction(RumActionType::Click, "Button");
      rum->StopAction(RumActionType::Click, "Button");

      rum->AddError(RumErrorSource::Source, "Oh no", "BadError");

      rum->StartResource(
          "foo", RumResourceMethod::Get, "http://localhost:8080", attributes
      );
      rum->StopResource("foo", 200, 65535, RumResourceType::Document, attributes);
      rum->StopResourceWithError(
          "foo", "Bad times", "RuntimeError", "", false, 0, attributes
      );

      rum->StartOperation("checkout");
      rum->SucceedOperation("checkout");
      rum->FailOperation("upload", RumOperationFailureReason::Error);
    }
  }
}

TEST_CASE("Rum::Register", "[unit][rum][cpp-api]") {
  SECTION("M accept config W all required values are present") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // And a valid Rum config
    RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");

    // When we register the RUM feature
    auto rum = Rum::Register(core, config);

    // Then we get a valid Rum interface
    REQUIRE(rum != nullptr);
  }

  SECTION("M reject config W required value not set") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // And a Rum config that lacks a valid application ID
    RumConfig config("this-is-not-a-valid-uuid-so-application-id-will-default-to-zero");

    // When we register the RUM feature
    auto rum = Rum::Register(core, config);

    // Then we get a valid pointer to a no-op Rum interface
    REQUIRE(rum != nullptr);
  }
}

TEST_CASE("Rum usage when SDK not running", "[unit][rum][cpp-api]") {
  // Given an ordinary RUM config
  RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");

  // And a started SDK with RUM initialized from that config
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  auto core = CoreTestHarness::WrapForCpp(test);
  auto rum = Rum::Register(core, config);
  REQUIRE(rum);

  // And a series of RUM API calls that should be no-ops when the SDK isn't running
  auto test_func = [](std::shared_ptr<Rum>& rum) {
    rum->StartView("foo", "Foo");
    rum->AddAttribute("attr1", Attribute::Int(100));
    rum->RemoveAttribute("attr1");
    rum->StartView("bar", "Bar");
    rum->AddViewAttribute("attr2", Attribute::Int(100));
    rum->AddAction(RumActionType::Custom, "action1");
    rum->StartResource("res1", RumResourceMethod::Post, "http://api/foo");
    rum->StopResource("res1", 204, 0, RumResourceType::Fetch);
    rum->RemoveViewAttribute("attr2");
    rum->StopView("bar");
    rum->StopSession();
    rum->StartView("foo", "Foo");
    rum->StartResource("res2", RumResourceMethod::Post, "http://api/bar");
    rum->StopResourceWithError("res2", "Invalid", "BadError", "", false, 0);
    rum->StartAction(RumActionType::Scroll, "scroll1");
    rum->StopAction(RumActionType::Scroll, "scroll1");
    rum->AddError(RumErrorSource::Console, "Internal error", "66");
    rum->StartOperation("checkout");
    rum->SucceedOperation("checkout");
    rum->FailOperation("upload", RumOperationFailureReason::Error);
  };

  SECTION("M be safe to call RUM API W SDK not yet started") {
    // When we make a bunch of RUM API calls prior to SDK initialization
    test_func(rum);

    // And then start and stop the SDK
    REQUIRE(core->Start());
    core->Stop();

    // Then no crashes occur, and no RUM events are produced
    REQUIRE(test.client.requests.empty());
  }

  SECTION("M be safe to call RUM API W SDK already stopped") {
    // When we start and then stop the SDK
    REQUIRE(core->Start());
    core->Stop();

    // And then make a bunch of RUM API calls after SDK shutdown
    test_func(rum);

    // And then restart and stop the SDK once more for good measure
    REQUIRE(core->Start());
    core->Stop();

    // Then no crashes occur, and no RUM events are produced
    REQUIRE(test.client.requests.empty());
  }
}

TEST_CASE("Rum argument validation", "[unit][rum][cpp-api]") {
  auto with_rum = [](RumConfig& config,
                     std::shared_ptr<Core>& core,
                     std::function<void(std::shared_ptr<Rum>)> func) {
    auto rum = Rum::Register(core, config);
    REQUIRE(rum);
    core->Start();
    func(rum);
    core->Stop();
  };

  // Given a series of tests consisting of a set of API calls and the warnings and/or
  // errors we expect to get in response
  struct TestParams {
    std::string_view name;
    std::function<void(RumConfig&, std::shared_ptr<Core>&)> func;
    std::vector<std::string_view> want_warnings;
    std::vector<std::string_view> want_errors;
  };
  std::vector<TestParams> tests = {

      // === Basic usage with no errors/warnings expected ===

      {"M print no warnings or errors W used normally",
       [](RumConfig& config, std::shared_ptr<Core>& core) {
         // Register RUM and start the SDK
         auto rum = Rum::Register(core, config);
         core->Start();

         // Add and remove global attributes
         rum->AddAttribute("foo", Attribute::Int(100));
         rum->AddAttribute("bar", Attribute::Int(200));
         rum->RemoveAttribute("foo");

         // Start a view with attributes
         Attribute view_attributes = Attribute::Object(1);
         view_attributes.SetObjectProperty("bar", Attribute::String("hello"));
         rum->StartView("my-view", "My View", view_attributes);

         // Modify view attributes
         rum->AddViewAttribute("baz", Attribute::String("world"));
         rum->RemoveViewAttribute("bar");

         // Stop the view
         rum->StopView("my-view");

         // Shut down the SDK
         core->Stop();
       },
       // All of the above should complete with 0 warnings and 0 errors
       {},
       {}},

      // === Register() ===

      {"M print init error W configured application_id is empty string",
       [](RumConfig& config, std::shared_ptr<Core>& core) {
         config.SetApplicationId("");
         Rum::Register(core, config);
       },
       {},
       {"RUM initialization failed: application_id value supplied via RumConfig "
        "must "
        "be a valid, nonzero UUID"}},

      {"M print init error W configured application_id is invalid UUID",
       [](RumConfig& config, std::shared_ptr<Core>& core) {
         config.SetApplicationId("not-a-valid-uuid");
         Rum::Register(core, config);
       },
       {},
       {"RUM initialization failed: application_id value supplied via RumConfig "
        "must "
        "be a valid, nonzero UUID"}},

      {"M print init error W configured application_id is nil UUID",
       [](RumConfig& config, std::shared_ptr<Core>& core) {
         config.SetApplicationId("00000000-0000-0000-0000-000000000000");
         Rum::Register(core, config);
       },
       {},
       {"RUM initialization failed: application_id value supplied via RumConfig "
        "must "
        "be a valid, nonzero UUID"}},

      // === StartView() ===

      {"M print warning W StartView is called with empty key",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("", "My View");
         });
       },
       {"Rum::StartView call ignored: application must supply a non-empty view "
        "key"},
       {}},

      {"M print no warning W StartView is called with empty view name",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "");
         });
       },
       {},
       {}},

      // === StopView() ===

      {"M print warning W StopView is called with empty key",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) { rum->StopView(""); });
       },
       {"Rum::StopView call ignored: application must supply a non-empty view key"},
       {}},

      // === AddAction() / StartAction() ===

      {"M print warning W AddAction is called with empty name",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->AddAction(RumActionType::Custom, "");
         });
       },
       {"Rum::AddAction call ignored: application must supply a non-empty action "
        "name"},
       {}},

      {"M print warning W StartAction is called with empty name",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->StartAction(RumActionType::Custom, "");
         });
       },
       {"Rum::StartAction call ignored: application must supply a non-empty action "
        "name"},
       {}},

      // === StartResource() / StopResource() / StopResourceWithError() ===

      {"M print warning W StartResource is called with empty key",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->StartResource("", RumResourceMethod::Get, "http://localhost:5000/foo");
         });
       },
       {"Rum::StartResource call ignored: application must supply a non-empty resource "
        "key"},
       {}},

      {"M print warning W StartResource is called with empty URL",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->StartResource("foo", RumResourceMethod::Get, "");
         });
       },
       {"Rum::StartResource call ignored: application must supply a non-empty URL"},
       {}},

      {"M print warning W StopResource is called with empty key",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->StopResource("");
         });
       },
       {"Rum::StopResource call ignored: application must supply a non-empty resource "
        "key"},
       {}},

      {"M print warning W StopResourceWithError is called with empty key",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->StopResourceWithError("", "Connection failed");
         });
       },
       {"Rum::StopResourceWithError call ignored: application must supply a non-empty "
        "resource key"},
       {}},

      {"M print warning W StopResourceWithError is called with empty error message",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->StopResourceWithError("foo", "");
         });
       },
       {"Rum::StopResourceWithError recording an error with no message: application "
        "should supply a non-empty error message"},
       {}},

      // === AddError() ===

      {"M print warning W AddError is called with empty error message",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->AddError(RumErrorSource::Source, "", "Error");
         });
       },
       {"Rum::AddError recording an error with no message: application should supply a "
        "non-empty error message"},
       {}},

      // === StartOperation() / SucceedOperation() /
      // FailOperation()
      // ===

      {"M print error W StartOperation is called with empty name",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->StartOperation("");
         });
       },
       {},
       {"Rum::StartOperation call ignored: application must supply a non-empty "
        "operation name"}},

      {"M print error W StartOperation is called with whitespace-only name",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->StartOperation("   ");
         });
       },
       {},
       {"Rum::StartOperation call ignored: application must supply a non-empty "
        "operation name"}},

      {"M print error W StartOperation is called with whitespace-only key",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->StartOperation("checkout", "   ");
         });
       },
       {},
       {"Rum::StartOperation call ignored: operation_key, if provided, must be "
        "a non-empty string"}},

      {"M print error W SucceedOperation is called with empty name",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->SucceedOperation("");
         });
       },
       {},
       {"Rum::SucceedOperation call ignored: application must supply a "
        "non-empty operation name"}},

      {"M print error W SucceedOperation is called with whitespace-only name",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->SucceedOperation("  \t  ");
         });
       },
       {},
       {"Rum::SucceedOperation call ignored: application must supply a "
        "non-empty operation name"}},

      {"M print error W FailOperation is called with empty name",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->FailOperation("", RumOperationFailureReason::Error);
         });
       },
       {},
       {"Rum::FailOperation call ignored: application must supply a non-empty "
        "operation name"}},

      {"M print error W FailOperation is called with whitespace-only name",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           rum->FailOperation("\n", RumOperationFailureReason::Abandoned);
         });
       },
       {},
       {"Rum::FailOperation call ignored: application must supply a non-empty "
        "operation name"}},

      {"M print no error W StartOperation is called with empty key",
       [&](RumConfig& config, std::shared_ptr<Core>& core) {
         with_rum(config, core, [](std::shared_ptr<Rum> rum) {
           rum->StartView("my-view", "My View");
           // Empty key means "no key" — valid
           rum->StartOperation("checkout", "");
         });
       },
       {},
       {}},
  };
  for (const auto& tt : tests) {
    DYNAMIC_SECTION(tt.name) {
      // Given a default RUM config and a core
      RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      auto core = CoreTestHarness::WrapForCpp(test);

      // When we execute our test function to exercise the RUM API
      tt.func(config, core);

      // Then we get the expected set of diagnostic warnings and errors
      REQUIRE(test.c_diagnostics.size() == 0);
      DiagnosticAsserts diagnostics = test.Diagnostics();
      diagnostics.RequireWarnings(tt.want_warnings);
      diagnostics.RequireErrors(tt.want_errors);
    }
  }
}

static nlohmann::json filter_events(std::string_view type, const nlohmann::json& xs) {
  nlohmann::json result = nlohmann::json::array();
  std::copy_if(
      xs.begin(),
      xs.end(),
      std::back_inserter(result),
      [type](const nlohmann::json& x) {
        return x.contains("type") && x["type"].get<std::string>() == type;
      }
  );
  return result;
}

TEST_CASE("Rum events", "[unit][rum][cpp-api]") {
  struct TestParams {
    std::string_view name;
    std::function<void(RumConfig&)> config_func;
    std::function<void(std::shared_ptr<Rum>&, MockClock&)> func;
    std::function<void(const nlohmann::json&)> assert_func;
  };
  std::vector<TestParams> tests = {

      // === Basic view event validation ===

      {"M send initial view event W new view is started",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we create a RUM view
         rum->StartView("my-view", "My View");
       },
       [](const nlohmann::json& events) {
         // Then RUM produces exactly one view event
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         RequireEventMatch(events[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "view",
            "date": 1700000000000,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View",
              "is_active": true,
              "time_spent": 0,
              "action": {"count": 0},
              "error": {"count": 0},
              "resource": {"count": 0}
            },
            "_dd": {
              "format_version": 2,
              "document_version": 0
            }
          })"));
       }},

      {"M send final view event W view is stopped",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and then later stop it
         rum->StartView("my-view", "My View");
         clock.Tick(std::chrono::seconds(15));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then RUM produces two events
         REQUIRE(events.size() == 2);
         // And the first describes the state of the view upon creation
         RequireEventMatch(events[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "view",
            "date": 1700000000000,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View",
              "is_active": true,
              "time_spent": 0,
              "action": {"count": 0},
              "error": {"count": 0},
              "resource": {"count": 0}
            },
            "_dd": {
              "format_version": 2,
              "document_version": 0
            }
          })"));
         // And the second describes the state of the view at its end, with 'is_active'
         // false, 'time_spent' reflecting the passage of 15 seconds, and
         // '_dd.document_version' incremented; while 'date' remains stable
         RequireEventMatch(events[1], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "view",
            "date": 1700000000000,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View",
              "is_active": false,
              "time_spent": 15000000000,
              "action": {"count": 0},
              "error": {"count": 0},
              "resource": {"count": 0}
            },
            "_dd": {
              "format_version": 2,
              "document_version": 1
            }
          })"));
         // And the session and view IDs are identical between those two events
         REQUIRE(events[0]["session"]["id"] == events[1]["session"]["id"]);
         REQUIRE(events[0]["view"]["id"] == events[1]["view"]["id"]);
       }},

      {"M send final + initial event W new view replaces previous view",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and then later replace it with a different view
         rum->StartView("my-view", "My View");
         clock.Tick(std::chrono::seconds(15));
         rum->StartView("my-other-view", "My Other View");
       },
       [](const nlohmann::json& events) {
         // Then RUM produces three events: a start and stop for 'my-view', and a start
         // for 'my-other-view'
         REQUIRE(events.size() == 3);
         REQUIRE(events[0]["type"] == "view");
         REQUIRE(events[1]["type"] == "view");
         REQUIRE(events[2]["type"] == "view");

         // All three events belong to the same session
         REQUIRE(events[0]["session"]["id"] == events[1]["session"]["id"]);
         REQUIRE(events[0]["session"]["id"] == events[2]["session"]["id"]);

         // The first two events are for the same view
         REQUIRE(events[0]["view"]["id"] == events[1]["view"]["id"]);

         // The last event represents the start of a distinct view
         REQUIRE(events[0]["view"]["id"] != events[2]["view"]["id"]);
       }},

      // === Session sampling ===

      {"M send 200 view events W 100 views are started and stopped",
       [](RumConfig&) {
         // Given a RUM config with the default sample rate of 100%
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we start and stop 100 views
         for (int i = 0; i < 100; i++) {
           std::string view_key = "view-" + std::to_string(i);
           rum->StartView(view_key);
           rum->StopView(view_key);
         }
       },
       [](const nlohmann::json& events) {
         // Then RUM sends exactly 200 view events: a start and a stop for each
         REQUIRE(events.size() == 200);
       }},

      {"M send roughly 2/3 of view events W session sample rate is 66%",
       [](RumConfig& config) {
         // Given a RUM config with a 66% session sample rate
         config.SetSessionSampleRate(66.0f);
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create 1000 views, using StopSession (which will emit a final view
         // event) to ensure that we create an independent session for each view
         for (int i = 0; i < 1000; i++) {
           std::string view_key = "view-" + std::to_string(i);
           rum->StartView(view_key);
           clock.Tick(std::chrono::seconds(1));
           rum->StopSession();
         }
       },
       [](const nlohmann::json& events) {
         // Then RUM produces roughly 1320 events: 2 for each sampled session, with
         // approximately 660 sessions sampled
         REQUIRE(events.size() > 1320 - 200);
         REQUIRE(events.size() < 1320 + 200);
       }},

      // === Inclusion of view attributes via StartView/StopView ===

      {"M include view attributes in view event W set via StartView",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we start a new view with {"foo":100}
         Attribute start_view_attributes = Attribute::Object(4);
         start_view_attributes.SetObjectProperty("foo", Attribute::Int(100));
         rum->StartView("my-view", "My View", start_view_attributes);
       },
       [](const nlohmann::json& events) {
         // Then our initial view event should have {"foo":100}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         REQUIRE(events[0]["context"] == nlohmann::json{{"foo", 100}});
       }},

      {"M isolate view attributes to target view W StartView starts new view",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we start a new view "my-view" with {"foo":100}
         Attribute my_view_attributes = Attribute::Object(4);
         my_view_attributes.SetObjectProperty("foo", Attribute::Int(100));
         rum->StartView("my-view", "My View", my_view_attributes);

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we start another view "other-view" with {"bar":200}
         Attribute other_view_attributes = Attribute::Object(4);
         other_view_attributes.SetObjectProperty("bar", Attribute::Int(200));
         rum->StartView("other-view", "Other View", other_view_attributes);
       },
       [](const nlohmann::json& events) {
         // Then 'my-view' is started and stopped, producing view events both times, and
         // 'other-view' produces a single event on creation
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 3);
         REQUIRE(events[0]["view"]["url"] == "my-view");
         REQUIRE(events[0]["view"]["is_active"] == true);
         REQUIRE(events[1]["view"]["url"] == "my-view");
         REQUIRE(events[1]["view"]["is_active"] == false);
         REQUIRE(events[2]["view"]["url"] == "other-view");
         REQUIRE(events[2]["view"]["is_active"] == true);

         // And both events for 'my-view' have {"foo":100}: they do _not_ adopt the
         // attributes in the second StartView command, as those values are intended for
         // another view
         REQUIRE(events[0]["context"] == nlohmann::json{{"foo", 100}});
         REQUIRE(events[1]["context"] == nlohmann::json{{"foo", 100}});

         // And the event for 'other-view' has {"bar":200}: prior attributes were
         // targeted only to the previous view
         REQUIRE(events[2]["context"] == nlohmann::json{{"bar", 200}});
       }},

      {"M isolate view attributes to target view W StartView starts new view {with "
       "identical key}",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we start a new view "my-view" with {"foo":100}
         Attribute my_view_attributes = Attribute::Object(4);
         my_view_attributes.SetObjectProperty("foo", Attribute::Int(100));
         rum->StartView("my-view", "My View", my_view_attributes);

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we start another view, also using the key "my-view", with {"bar":200}
         Attribute my_view_2_attributes = Attribute::Object(4);
         my_view_2_attributes.SetObjectProperty("bar", Attribute::Int(200));
         rum->StartView("my-view", "My View", my_view_2_attributes);
       },
       [](const nlohmann::json& events) {
         // Then we still have two distinct views, both labeled 'my-view': the first one
         // starts and ends, and the second one starts
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 3);
         REQUIRE(events[0]["view"]["url"] == "my-view");
         REQUIRE(events[0]["view"]["is_active"] == true);
         REQUIRE(events[1]["view"]["url"] == "my-view");
         REQUIRE(events[1]["view"]["is_active"] == false);
         REQUIRE(events[2]["view"]["url"] == "my-view");
         REQUIRE(events[2]["view"]["id"] != events[0]["view"]["id"]);
         REQUIRE(events[2]["view"]["is_active"] == true);

         // And both events for the first 'my-view' have {"foo":100}: they do _not_
         // adopt the attributes in the second StartView command, regardless of the fact
         // that the target view key is identical: the second StartView command still
         // targets an entirely separate view
         REQUIRE(events[0]["context"] == nlohmann::json{{"foo", 100}});
         REQUIRE(events[1]["context"] == nlohmann::json{{"foo", 100}});

         // And the second 'my-view' has {"bar":200}: view-level attributes are limited
         // to the lifetime of a single view scope; they are _not_ stored persistently
         // and reused for later views with the same key
         REQUIRE(events[2]["context"] == nlohmann::json{{"bar", 200}});
       }},

      {"M include view attributes in view event W set via StopView",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we start a new view with {"foo":100,"bar":200}
         Attribute start_view_attributes = Attribute::Object(4);
         start_view_attributes.SetObjectProperty("foo", Attribute::Int(100));
         start_view_attributes.SetObjectProperty("bar", Attribute::Int(200));
         rum->StartView("my-view", "My View", start_view_attributes);

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we issue a StopView call for that same view, with {"bar":300,"baz":400}
         Attribute stop_view_attributes = Attribute::Object(4);
         stop_view_attributes.SetObjectProperty("bar", Attribute::Int(300));
         stop_view_attributes.SetObjectProperty("baz", Attribute::Int(400));
         rum->StopView("my-view", stop_view_attributes);
       },
       [](const nlohmann::json& events) {
         // Then our view has two events, one for start and one for stop
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);
         REQUIRE(events[0]["view"]["is_active"] == true);
         REQUIRE(events[1]["view"]["is_active"] == false);

         // And the first event reflects the attributes that were applied to the view at
         // its birth: {"foo":100,"bar":200}
         REQUIRE(events[0]["context"] == nlohmann::json{{"foo", 100}, {"bar", 200}});

         // And the second event has {"foo":100,"bar":300,"baz":400}, since attributes
         // supplied on StopView are merged into the existing set of view-level
         // attributes
         REQUIRE(
             events[1]["context"] ==
             nlohmann::json{{"foo", 100}, {"bar", 300}, {"baz", 400}}
         );
       }},

      // === Inclusion of view attributes via AddViewAtribute/RemoveViewAttribute ===

      {"M include view attributes in view event W set via AddViewAtribute",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we start a new view with no attributes
         rum->StartView("my-view", "My View");

         // And we then set {"foo":100} on the view after its creation
         rum->AddViewAttribute("foo", Attribute::Int(100));

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we issue a StopView call to produce another view event
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then we get two view events, one at start and one at stop
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);

         // And the first has no user attributes since it occured before our call
         REQUIRE(!events[0].contains("context"));

         // And the second has {"foo":100} since it occurred after our call
         REQUIRE(events[1].contains("context"));
         REQUIRE(events[1]["context"] == nlohmann::json{{"foo", 100}});
       }},

      {"M modify existing view attribute value W AddViewAtribute called for existing "
       "attribute",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we start a new view with no attributes
         rum->StartView("my-view", "My View");

         // And we then set {"foo":100} on the view after its creation
         rum->AddViewAttribute("foo", Attribute::Int(100));

         // And we then set {"foo":200} immediately thereafter
         rum->AddViewAttribute("foo", Attribute::Int(200));

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then our final view event has {"foo":200}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);
         REQUIRE(events[1]["context"] == nlohmann::json{{"foo", 200}});
       }},

      {"M remove existing view attribute value W RemoveViewAttribute called for "
       "existing attribute",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we start a new view with no attributes
         rum->StartView("my-view", "My View");

         // And we then set {"foo":100,"bar":200} on the view after its creation
         rum->AddViewAttribute("foo", Attribute::Int(100));
         rum->AddViewAttribute("bar", Attribute::Int(200));

         // And we then delete "foo" immediately thereafter
         rum->RemoveViewAttribute("foo");

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then our final view event has {"bar":200}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);
         REQUIRE(events[1]["context"] == nlohmann::json{{"bar", 200}});
       }},

      {"M do nothing W RemoveViewAttribute called for nonexistent attribute",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we start a new view with no attributes
         rum->StartView("my-view", "My View");

         // And we then attempt to delete an attribute called "foo", which doens't exist
         rum->RemoveViewAttribute("foo");

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then our final view event has no user attributes
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);
         REQUIRE(!events[1].contains("context"));
       }},

      {"M mutate view attributes W start/attr/stop funcs are called successively",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we start a new view with {"able":1,"baker":2,"charlie":3,"dog":4}
         Attribute start_view_obj = Attribute::Object(4);
         start_view_obj.SetObjectProperty("able", Attribute::Int(1));
         start_view_obj.SetObjectProperty("baker", Attribute::Int(2));
         start_view_obj.SetObjectProperty("charlie", Attribute::Int(3));
         start_view_obj.SetObjectProperty("dog", Attribute::Int(4));
         rum->StartView("my-view", "My View", start_view_obj);

         // And we then delete "baker" and set {"charlie":"modified"} and {"dog":444}
         rum->RemoveViewAttribute("baker");
         rum->AddViewAttribute("charlie", Attribute::String("modified"));
         rum->AddViewAttribute("dog", Attribute::Int(444));

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we then stop the view, supplying {"dog":98,"easy":99}
         Attribute stop_view_obj = Attribute::Object(2);
         stop_view_obj.SetObjectProperty("dog", Attribute::Int(98));
         stop_view_obj.SetObjectProperty("easy", Attribute::Int(99));
         rum->StopView("my-view", stop_view_obj);
       },
       [](const nlohmann::json& events) {
         // Then we get two view events, one at start and one at stop
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);

         // And the first has {"able":1,"baker":2,"charlie":3,"dog":4}, reflecting our
         // initial set of attributes at view creation
         REQUIRE(
             events[0]["context"] ==
             nlohmann::json{{"able", 1}, {"baker", 2}, {"charlie", 3}, {"dog", 4}}
         );

         // And the second has {"able":1,"charlie":"modified","dog:98,"easy":99},
         // reflecting the deletion of baker, the modification of charlie, the final
         // prevailing value of dog, and the addition of easy
         REQUIRE(
             events[1]["context"] ==
             nlohmann::json{
                 {"able", 1}, {"charlie", "modified"}, {"dog", 98}, {"easy", 99}
             }
         );
       }},

      // === Inclusion of global attributes; merging of global and view attributes ===

      {"M include global attributes in view events W AddAttribute called",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we add a global attribute {"x":"hello"}
         rum->AddAttribute("x", Attribute::String("hello"));

         // And we start a new view with {"foo":100}
         Attribute start_view_attributes = Attribute::Object(4);
         start_view_attributes.SetObjectProperty("foo", Attribute::Int(100));
         rum->StartView("my-view", "My View", start_view_attributes);
       },
       [](const nlohmann::json& events) {
         // Then our initial view event should have {"x":"hello","foo":100}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         REQUIRE(events[0]["context"] == nlohmann::json{{"x", "hello"}, {"foo", 100}});
       }},

      {"M modify global attribute value W AddAttribute called for existing attribute",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we add a global attribute {"x":"hello"}
         rum->AddAttribute("x", Attribute::String("hello"));

         // And we start a new view with {"foo":100}
         Attribute start_view_attributes = Attribute::Object(4);
         start_view_attributes.SetObjectProperty("foo", Attribute::Int(100));
         rum->StartView("my-view", "My View", start_view_attributes);

         // And we update our global attribute to {"x":"world"}
         rum->AddAttribute("x", Attribute::String("world"));

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then we get two view events
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);

         // And the first has {"x":"hello","foo":100}
         REQUIRE(events[0]["context"] == nlohmann::json{{"x", "hello"}, {"foo", 100}});

         // And the last has {"x":"world","foo":100}
         REQUIRE(events[1]["context"] == nlohmann::json{{"x", "world"}, {"foo", 100}});
       }},

      {"M remove global attribute value W RemoveAttribute called for existing "
       "attribute",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we set global attributes {"x":"hello","y":"world"}
         rum->AddAttribute("x", Attribute::String("hello"));
         rum->AddAttribute("y", Attribute::String("world"));

         // And we start a new view with {"foo":100}
         Attribute start_view_attributes = Attribute::Object(4);
         start_view_attributes.SetObjectProperty("foo", Attribute::Int(100));
         rum->StartView("my-view", "My View", start_view_attributes);

         // And we then delete the global attribute "x"
         rum->RemoveAttribute("x");

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then we get two view events
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);

         // And the first has {"x":"hello","y":"world","foo":100}
         REQUIRE(
             events[0]["context"] ==
             nlohmann::json{{"x", "hello"}, {"y", "world"}, {"foo", 100}}
         );

         // And the last has {"y":"world","foo":100}
         REQUIRE(events[1]["context"] == nlohmann::json{{"y", "world"}, {"foo", 100}});
       }},

      {"M do nothing W RemoveAttribute called for nonexistent attribute",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we attempt to remove a global attribute that doesn't exist
         rum->RemoveAttribute("foo");
       },
       [](const nlohmann::json& events) {
         // Then nothing happens
         REQUIRE(events.is_null());
       }},

      {"M merge view attributes into global attributes",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we add global attributes {"alpha":1,"bravo":2,"charlie":3}
         rum->AddAttribute("alpha", Attribute::Int(1));
         rum->AddAttribute("bravo", Attribute::Int(2));
         rum->AddAttribute("charlie", Attribute::Int(3));

         // And we start a new view with {"able":100,"baker":200,"charlie":300}
         Attribute start_view_obj = Attribute::Object(3);
         start_view_obj.SetObjectProperty("able", Attribute::Int(100));
         start_view_obj.SetObjectProperty("baker", Attribute::Int(200));
         start_view_obj.SetObjectProperty("charlie", Attribute::Int(300));
         rum->StartView("my-view", "My View", start_view_obj);

         // And we then remove the global "alpha"
         rum->RemoveAttribute("alpha");

         // And we update the global "bravo" to "modified"
         rum->AddAttribute("bravo", Attribute::String("modified"));

         // And we remove the view-level "charlie"
         rum->RemoveViewAttribute("charlie");

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then we get two view events
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);

         // And the first has {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         REQUIRE(
             events[0]["context"] == nlohmann::json{
                                         {"alpha", 1},
                                         {"bravo", 2},
                                         {"able", 100},
                                         {"baker", 200},
                                         {"charlie", 300}
                                     }
         );

         // And the last has {"bravo":"modified","able":100,"baker":200,"charlie":3},
         // reflecting the deletion of alpha, the mutation of bravo, and the return to
         // the global value of charlie after the deletion of the view-level attribute
         // that was shadowing it
         REQUIRE(
             events[1]["context"] ==
             nlohmann::json{
                 {"bravo", "modified"}, {"able", 100}, {"baker", 200}, {"charlie", 3}
             }
         );
       }},

      // === Freezing of attribute values upon view inactivity ===

      {"M freeze attribute values W view is stopped while scope remains open due to "
       "pending resources",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we set global attributes {"alpha":1,"bravo":2,"charlie":3}
         rum->AddAttribute("alpha", Attribute::Int(1));
         rum->AddAttribute("bravo", Attribute::Int(2));
         rum->AddAttribute("charlie", Attribute::Int(3));

         // And we start a view with {"able":100,"baker":200,"charlie":300}
         Attribute start_view_obj = Attribute::Object(3);
         start_view_obj.SetObjectProperty("able", Attribute::Int(100));
         start_view_obj.SetObjectProperty("baker", Attribute::Int(200));
         start_view_obj.SetObjectProperty("charlie", Attribute::Int(300));
         rum->StartView("my-view", "My View", start_view_obj);

         // And we start a resource within the active view
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And 15 seconds later, we create a new view with the same key, thereby
         // stopping the original view while it still has pending resources
         clock.Tick(std::chrono::seconds(15));
         rum->StartView("my-view", "My View");

         // And we modify global attributes after the original view has stopped,
         // deleting "alpha" and adding {"delta":4}
         rum->RemoveAttribute("alpha");
         rum->AddAttribute("delta", Attribute::Int(4));

         // And we attempt to modify view attributes after the view has stopped, trying
         // to delete "able" and trying to add {"dog":400}
         rum->RemoveViewAttribute("able");
         rum->AddViewAttribute("dog", Attribute::Int(400));

         // And 1 second later, we stop the resource, allowing our original view scope
         // to close
         clock.Tick(std::chrono::seconds(1));
         rum->StopResource("get-profile-123", 200, 12345, RumResourceType::Xhr);

         // And finally, 5 seconds after that, we close the new view
         clock.Tick(std::chrono::seconds(5));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then we get the following sequence of RUM events:
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 6);

         // - At T+0: a `view` with:
         //   - _dd.document_version: 0
         //   - view.is_active: true
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         REQUIRE(events[0]["type"] == "view");
         REQUIRE(events[0]["_dd"]["document_version"] == 0);
         REQUIRE(events[0]["view"]["is_active"] == true);
         REQUIRE(
             events[0]["context"] == nlohmann::json{
                                         {"alpha", 1},
                                         {"bravo", 2},
                                         {"able", 100},
                                         {"baker", 200},
                                         {"charlie", 300}
                                     }
         );
         const std::string& first_view_id_str =
             events[0]["view"]["id"].get_ref<const std::string&>();

         // - At T+15: a `view` with:
         //   - view.id: <same as previous event>
         //   - _dd.document_version: 1
         //   - view.is_active: true
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         REQUIRE(events[1]["type"] == "view");
         REQUIRE(events[1]["view"]["id"] == first_view_id_str);
         REQUIRE(events[1]["_dd"]["document_version"] == 1);
         REQUIRE(events[1]["view"]["is_active"] == true);
         REQUIRE(events[1]["context"] == events[0]["context"]);

         // - At T+15: a `view` with:
         //   - view.id: <different from prior view events>
         //   - _dd.document_version: 0
         //   - view.is_active: true
         //   - context: {"alpha":1,"bravo":2,"charlie":3}
         REQUIRE(events[2]["type"] == "view");
         REQUIRE(events[2]["view"]["id"] != first_view_id_str);
         REQUIRE(events[2]["_dd"]["document_version"] == 0);
         REQUIRE(events[2]["view"]["is_active"] == true);
         REQUIRE(
             events[2]["context"] ==
             nlohmann::json{{"alpha", 1}, {"bravo", 2}, {"charlie", 3}}
         );
         const std::string& second_view_id_str =
             events[2]["view"]["id"].get_ref<const std::string&>();

         // - At T+16: a `resource` recording successful completion, with:
         //   - view.id: <matching first view>
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         REQUIRE(events[3]["type"] == "resource");
         REQUIRE(events[3]["view"]["id"] == first_view_id_str);
         REQUIRE(events[3]["resource"]["method"] == "GET");
         REQUIRE(
             events[3]["resource"]["url"] ==
             "https://my-cool-website.biz/api/profile/123"
         );
         REQUIRE(events[3]["resource"]["duration"] == 16000000000);
         REQUIRE(events[3]["resource"]["type"] == "xhr");
         REQUIRE(events[3]["resource"]["status_code"] == 200);

         // - At T+16: a `view` with:
         //   - view.id: <matching first view>
         //   - _dd.document_version: 2
         //   - view.is_active: false
         //   - view.resource.count: 1
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         REQUIRE(events[4]["type"] == "view");
         REQUIRE(events[4]["view"]["id"] == first_view_id_str);
         REQUIRE(events[4]["_dd"]["document_version"] == 2);
         REQUIRE(events[4]["view"]["is_active"] == false);
         REQUIRE(events[4]["view"]["resource"]["count"] == 1);
         REQUIRE(events[4]["context"] == events[0]["context"]);

         // - At T+21: a `view` with:
         //   - view.id: <matching second view>
         //   - _dd.document_version: 1
         //   - view.is_active: false
         //   - context: {"bravo":2,"charlie":3,"delta":4,"dog":400}
         REQUIRE(events[5]["type"] == "view");
         REQUIRE(events[5]["view"]["id"] == second_view_id_str);
         REQUIRE(events[5]["_dd"]["document_version"] == 1);
         REQUIRE(events[5]["view"]["is_active"] == false);
         REQUIRE(
             events[5]["context"] ==
             nlohmann::json{{"bravo", 2}, {"charlie", 3}, {"delta", 4}, {"dog", 400}}
         );
       }},

      // === Retention of view attributes on session refresh ===

      {"M retain view attributes W last-active view is recreated after session refresh",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we set global attributes {"bravo":2,"charlie":3}
         rum->AddAttribute("bravo", Attribute::Int(2));
         rum->AddAttribute("charlie", Attribute::Int(3));

         // And we start a view with {"baker":200,"charlie":300}
         Attribute start_view_obj = Attribute::Object(3);
         start_view_obj.SetObjectProperty("baker", Attribute::Int(200));
         start_view_obj.SetObjectProperty("charlie", Attribute::Int(300));
         rum->StartView("my-view", "My View", start_view_obj);

         // And we allow 30 minutes to pass without user activity, such that on the next
         // user action we record, the initial session will be considered expired and a
         // new session will be created to replace it, with our original view being
         // recreated in that new session
         clock.Tick(std::chrono::minutes(30));

         // And we subsequently record a user interaction to trigger session refresh and
         // view transfer, making it a discrete custom attribute so it'll send an event
         // immediately, and providing {"baker":22,"dog":44} as action attributes
         Attribute add_action_obj = Attribute::Object(2);
         add_action_obj.SetObjectProperty("baker", Attribute::Int(22));
         add_action_obj.SetObjectProperty("dog", Attribute::Int(44));
         rum->AddAction(RumActionType::Custom, "instant!", add_action_obj);
       },
       [](const nlohmann::json& events) {
         // Then we get the following sequence of RUM events:
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 4);
         // - At T+0: a `view` with:
         //   - _dd.document_version: 0
         //   - view.is_active: true
         //   - view.name: "My View"
         //   - context: {"bravo":2,"baker":200,"charlie":300}
         REQUIRE(events[0]["type"] == "view");
         REQUIRE(events[0]["_dd"]["document_version"] == 0);
         REQUIRE(events[0]["view"]["is_active"] == true);
         REQUIRE(events[0]["view"]["name"] == "My View");
         REQUIRE(
             events[0]["context"] ==
             nlohmann::json{{"bravo", 2}, {"baker", 200}, {"charlie", 300}}
         );
         // - At T+1800: a `view` with:
         //   - _dd.document_version: 0
         //   - session.id: <distinct from the session.id value in the preceding event>
         //   - view.id: <distinct from the view.id value in the preceding event>
         //   - view.is_active: true
         //   - view.name: "My View"
         //   - context: {"bravo":2,"baker":200,"charlie":300}
         // TODO(RUM-12546): After view event deduplication, this event will be omitted
         REQUIRE(events[1]["type"] == "view");
         REQUIRE(events[1]["_dd"]["document_version"] == 0);
         REQUIRE(events[1]["session"]["id"] != events[0]["session"]["id"]);
         REQUIRE(events[1]["view"]["id"] != events[0]["view"]["id"]);
         REQUIRE(events[1]["view"]["is_active"] == true);
         REQUIRE(events[1]["view"]["action"]["count"] == 0);
         REQUIRE(events[1]["view"]["name"] == "My View");
         REQUIRE(
             events[1]["context"] ==
             nlohmann::json{{"bravo", 2}, {"baker", 200}, {"charlie", 300}}
         );
         // - At T+1800: an `action` with:
         //   - session.id: <equal to the session.id value in the latest view event>
         //   - view.id: <equal to the view.id value in the latest view event>
         //   - context: {"bravo":2,"baker":22,"charlie":300,"dog":44}
         REQUIRE(events[2]["type"] == "action");
         REQUIRE(events[2]["session"]["id"] == events[1]["session"]["id"]);
         REQUIRE(events[2]["view"]["id"] == events[1]["view"]["id"]);
         REQUIRE(
             events[2]["context"] ==
             nlohmann::json{{"bravo", 2}, {"baker", 22}, {"charlie", 300}, {"dog", 44}}
         );
         // - At T+1800: a duplicate `view` event with an incremented view.action.count
         // TODO(RUM-12546): After view event deduplication, this will be the only event
         // for the second view
         REQUIRE(events[3]["type"] == "view");
         REQUIRE(events[3]["view"]["action"]["count"] == 1);
         REQUIRE(events[3]["view"]["id"] == events[1]["view"]["id"]);
       }},

      // === Actions (continuous: StartAction(), StopAction()) ===

      {"M not send action event W action remains active",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we create a RUM view and record an action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Scroll, "scroll1");
       },
       [](const nlohmann::json& events) {
         // Then we don't end up with any action events, because an action scope only
         // sends a single event upon being closed
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         REQUIRE(events[0]["type"] == "view");
       }},

      {"M send action event W continuous action is stopped immediately",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we create a RUM view and record an action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Scroll, "scroll1");

         // And we then stop that action explicitly
         rum->StopAction(RumActionType::Scroll, "scroll1");
       },
       [](const nlohmann::json& events) {
         // Then RUM produces a single action event, along with at least one view event
         REQUIRE(events.is_array());
         REQUIRE(events.size() > 1);
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);

         // And the action event has all expected properties
         RequireEventMatch(actions[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "action",
            "date": 1700000000000,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View"
            },
            "action": {
              "id": "${__NONZERO_UUID__}",
              "type": "scroll",
              "target": {"name": "scroll1"},
              "loading_time": 0
            },
            "_dd": {
              "format_version": 2
            }
          })"));

         // And the last of our view events has an incremented action count
         auto last = events.back();
         REQUIRE(last["type"] == "view");
         REQUIRE(last["view"]["id"] == actions[0]["view"]["id"]);
         REQUIRE(last["view"]["action"]["count"] == 1);
       }},

      {"M send action event W continuous action is stopped after a delay",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and record an action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Scroll, "scroll1");

         // And we then stop that action explicitly after a 2-second delay
         clock.Tick(std::chrono::seconds(2));
         rum->StopAction(RumActionType::Scroll, "scroll1");
       },
       [](const nlohmann::json& events) {
         // Then RUM produces a single action event where the 'date' value reflects the
         // time at which the action started, and 'action.loading_time' is the count of
         // nanoseconds reflecting our action's 2-second lifetime
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 2000000000);
       }},

      {"M send action event W command is processed >=10s after continuous action start",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and record an action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Scroll, "scroll1");

         // And we wait 15 seconds and initiate any RUM operation that will result in a
         // command being processed by the active action scope
         clock.Tick(std::chrono::seconds(15));
         rum->RemoveViewAttribute("nonexistent");
       },
       [](const nlohmann::json& events) {
         // Then an action event gets sent, and its duration is clamped at 10s
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 10000000000);
       }},

      {"M not send action event W command is processed <10s after continuous action "
       "start",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and record an action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Scroll, "scroll1");

         // And then 4s later, we initiate any RUM operation that will result in a
         // command being processed by the active action scope
         clock.Tick(std::chrono::seconds(4));
         rum->RemoveViewAttribute("nonexistent");
       },
       [](const nlohmann::json& events) {
         // Then we don't end up with any action events, because at T+4s, the scope for
         // our continuous action is still active
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         REQUIRE(events[0]["type"] == "view");
       }},

      // === Actions (discrete: AddAction()) ===

      {"M send immediate action event W discrete action has a type of custom",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we create a RUM view and add a custom discrete action
         rum->StartView("my-view", "My View");
         rum->AddAction(RumActionType::Custom, "custom1");
       },
       [](const nlohmann::json& events) {
         // Then an action event is sent immediately
         REQUIRE(events.is_array());
         REQUIRE(events.size() > 1);
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);

         // And the action event has all expected properties
         RequireEventMatch(actions[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "action",
            "date": 1700000000000,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View"
            },
            "action": {
              "id": "${__NONZERO_UUID__}",
              "type": "custom",
              "target": {"name": "custom1"},
              "loading_time": 0
            },
            "_dd": {
              "format_version": 2
            }
          })"));
       }},

      {"M not send immediate action event W discrete action has a type other than "
       "custom",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we create a RUM view and add a discrete action whose type is not custom
         rum->StartView("my-view", "My View");
         rum->AddAction(RumActionType::Click, "button1");
       },
       [](const nlohmann::json& events) {
         // Then we don't end up with any action events, because an action scope only
         // sends a single event upon being closed, and even a discrete action has a
         // brief lifetime
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         REQUIRE(events[0]["type"] == "view");
       }},

      {"M send action event W any command is processed >=100ms after discrete action",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and add a discrete action whose type is not custom
         rum->StartView("my-view", "My View");
         rum->AddAction(RumActionType::Click, "button1");

         // And then 150ms later, we initiate any RUM operation that will result in a
         // command being processed by the active action scope
         clock.Tick(std::chrono::milliseconds(150));
         rum->RemoveViewAttribute("nonexistent");
       },
       [](const nlohmann::json& events) {
         // Then an action event gets sent, and its duration is clamped at 100ms
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 100000000);
       }},

      {"M send action event W discrete action is explicitly stopped",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and add a discrete action whose type is not custom
         rum->StartView("my-view", "My View");
         rum->AddAction(RumActionType::Click, "button1");

         // And then 50ms later, we explicitly stop the current action
         clock.Tick(std::chrono::milliseconds(50));
         rum->StopAction(RumActionType::Click, "button1");
       },
       [](const nlohmann::json& events) {
         // Then an action event gets sent, and its duration is 50ms: discrete actions
         // are subject to StopAction calls just the same continuous actions
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 50000000);
       }},

      {"M send discrete custom action event immediately W another action is active",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start a continuous custom action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Custom, "long-custom");

         // And then at T+2s, we add a discrete custom action
         clock.Tick(std::chrono::seconds(2));
         rum->AddAction(RumActionType::Custom, "instant-custom");

         // And then at T+4s, we stop our original continuous action
         clock.Tick(std::chrono::seconds(2));
         rum->StopAction(RumActionType::Custom, "long-custom");
       },
       [](const nlohmann::json& events) {
         // Then we end up with two action events
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 2);

         // And the first event to be sent describes our discrete action (since it
         // "ended" first), with a timestamp of T+2s and a duration of 0
         REQUIRE(actions[0]["action"]["target"]["name"] == "instant-custom");
         REQUIRE(actions[0]["date"] == 1700000002000);
         REQUIRE(actions[0]["action"]["loading_time"] == 0);

         // And the second event describes our continuous action, with an earlier
         // timestamp of T+0 and a duration of 4 seconds
         REQUIRE(actions[1]["action"]["target"]["name"] == "long-custom");
         REQUIRE(actions[1]["date"] == 1700000000000);
         REQUIRE(actions[1]["action"]["loading_time"] == 4000000000);
       }},

      // === Parameters passed to rum->StopAction() ===

      {"M stop current action W rum->StopAction alled, regardless of type",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action with type 'click'
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Click, "button1");

         // And then 50ms later, we explicitly stop the current action, passing a
         // different action type of 'swipe'
         clock.Tick(std::chrono::milliseconds(50));
         rum->StopAction(RumActionType::Swipe, "button1");
       },
       [](const nlohmann::json& events) {
         // Then the action is stopped and an action event gets sent, with the original
         // action type of 'click' retained: the 'type' parameter accepted by StopAction
         // serves no actual purpose
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 50000000);
         REQUIRE(actions[0]["action"]["type"] == "click");
         REQUIRE(actions[0]["action"]["target"]["name"] == "button1");
       }},

      {"M stop current action W StopAction is called with empty name",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action with name "button1"
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Click, "button1");

         // And then 50ms later, we explicitly stop the current action, passing no name
         clock.Tick(std::chrono::milliseconds(50));
         rum->StopAction(RumActionType::Click);
       },
       [](const nlohmann::json& events) {
         // Then the action is stopped and an action event gets sent, with the
         // original name of 'button1'
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 50000000);
         REQUIRE(actions[0]["action"]["type"] == "click");
         REQUIRE(actions[0]["action"]["target"]["name"] == "button1");
       }},

      {"M rename and stop current action W rum->StopAction alled with different "
       "name",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action with name "button1"
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Click, "button1");

         // And then 50ms later, we explicitly stop the current action, passing a
         // different name of "boton2"
         clock.Tick(std::chrono::milliseconds(50));
         rum->StopAction(RumActionType::Click, "boton2");
       },
       [](const nlohmann::json& events) {
         // Then the action is stopped and an action event gets sent, with the
         // newly-provided name of 'boton2' used in place of the original 'button1': the
         // 'name' parameter accepted by StopAction does _not_ describe the intended
         // target; it's applied to whatever action is active
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 50000000);
         REQUIRE(actions[0]["action"]["type"] == "click");
         REQUIRE(actions[0]["action"]["target"]["name"] == "boton2");
       }},

      // === Action events at view/session stop ===

      {"M stop current action W StopView is called",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Click, "button1");

         // And then 50ms later, we explicitly stop the current view
         clock.Tick(std::chrono::milliseconds(50));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then we end up with a single action event, as our action is cleanly stopped
         // as a side effect of the view being stopped
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 50000000);
         REQUIRE(actions[0]["action"]["type"] == "click");
         REQUIRE(actions[0]["action"]["target"]["name"] == "button1");

         // And the last of our view events has an incremented action count
         auto last = events.back();
         REQUIRE(last["type"] == "view");
         REQUIRE(last["view"]["id"] == actions[0]["view"]["id"]);
         REQUIRE(last["view"]["action"]["count"] == 1);
       }},

      {"M stop current action W StartView is called",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Click, "button1");

         // And then 50ms later, we start a new view, effectively ending the current one
         clock.Tick(std::chrono::milliseconds(50));
         rum->StartView("another-view", "Another View");
       },
       [](const nlohmann::json& events) {
         // Then we end up with a single action event, as our action is cleanly stopped
         // as a side effect of the view being stopped
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 50000000);
         REQUIRE(actions[0]["action"]["type"] == "click");
         REQUIRE(actions[0]["action"]["target"]["name"] == "button1");

         // And final view event for our original view has an incremented view count
         const nlohmann::json* last_event_for_original_view;
         for (const auto& event : events) {
           if (event["type"] == "view" &&
               event["view"]["id"] == actions[0]["view"]["id"]) {
             last_event_for_original_view = &event;
           }
         }
         REQUIRE(last_event_for_original_view);
         REQUIRE((*last_event_for_original_view)["view"]["name"] == "My View");
         REQUIRE((*last_event_for_original_view)["view"]["action"]["count"] == 1);

         // And we also have a view event for the newly-created view
         auto last = events.back();
         REQUIRE(last["type"] == "view");
         REQUIRE(last["view"]["id"] != actions[0]["view"]["id"]);
         REQUIRE(last["view"]["action"]["count"] == 0);
       }},

      {"M stop current action W StopSession is called",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Click, "button1");

         // And then 50ms later, we explicitly stop the session
         clock.Tick(std::chrono::milliseconds(50));
         rum->StopSession();
       },
       [](const nlohmann::json& events) {
         // Then we end up with a single action event, as our action is cleanly stopped
         // as a side effect of the view being stopped
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 50000000);
         REQUIRE(actions[0]["action"]["type"] == "click");
         REQUIRE(actions[0]["action"]["target"]["name"] == "button1");

         // And the last of our view events has an incremented action count
         auto last = events.back();
         REQUIRE(last["type"] == "view");
         REQUIRE(last["view"]["id"] == actions[0]["view"]["id"]);
         REQUIRE(last["view"]["action"]["count"] == 1);
       }},

      {"M drop current action W session expires",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Click, "button1");

         // And then 7h later, we attempt to stop the action
         clock.Tick(std::chrono::hours(7));
         rum->StopAction(RumActionType::Click, "button1");
       },
       [](const nlohmann::json& events) {
         // Then we get no action events: when a session expires with an action still
         // active, that action is simply dropped
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 0);
       }},

      // === Resources send 'resource' or 'error' on StopResource[WithError] ===

      {"M send no event W resource remains open",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we create a RUM view and record the start of a resource, without
         // recording the end of that resource
         rum->StartView("my-view", "My View");
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );
       },
       [](const nlohmann::json& events) {
         // Then no resource or error events are sent; only view events
         REQUIRE(filter_events("resource", events).size() == 0);
         REQUIRE(filter_events("error", events).size() == 0);
         REQUIRE(filter_events("view", events).size() == events.size());
       }},

      {"M send resource event W resource is ended via StopResource",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and record the start of a resource
         rum->StartView("my-view", "My View");
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And 2.4s later, we end that resource
         clock.Tick(std::chrono::milliseconds(2400));
         rum->StopResource("get-profile-123", 200, 12345, RumResourceType::Xhr);
       },
       [](const nlohmann::json& events) {
         // Then a resource event is sent to describe our finished resource
         REQUIRE(filter_events("error", events).size() == 0);
         auto resources = filter_events("resource", events);

         // And the resource event has all expected properties
         RequireEventMatch(resources[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "resource",
            "date": 1700000000000,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View"
            },
            "resource": {
              "id": "${__NONZERO_UUID__}",
              "method": "GET",
              "url": "https://my-cool-website.biz/api/profile/123",
              "duration": 2400000000,
              "status_code": 200,
              "size": 12345,
              "type": "xhr"
            },
            "_dd": {
              "format_version": 2
            }
          })"));
       }},

      {"M send error event W resource is ended via StopResourceWithError",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and record the start of a resource
         rum->StartView("my-view", "My View");
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And 2.4s later, we end that resource with an error
         clock.Tick(std::chrono::milliseconds(2400));
         rum->StopResourceWithError(
             "get-profile-123",
             "Invalid format",
             "ParseError",
             "this\nis\na\nstack\ntrace\n",
             false,
             200
         );
       },
       [](const nlohmann::json& events) {
         // Then an error event is sent to describe our finished resource
         REQUIRE(filter_events("resource", events).size() == 0);
         auto errors = filter_events("error", events);

         // And the error event has all expected error properties, plus relevant context
         // describing the resource, and its 'date' timestamp reflects error time, not
         // resource start time
         RequireEventMatch(errors[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "error",
            "date": 1700000002400,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View"
            },
            "error": {
              "resource": {
                "method": "GET",
                "url": "https://my-cool-website.biz/api/profile/123",
                "status_code": 200
              },
              "message": "Invalid format",
              "type": "ParseError",
              "stack": "this\nis\na\nstack\ntrace\n",
              "source": "network",
              "category": "Exception"
            },
            "_dd": {
              "format_version": 2
            }
          })"));
       }},

      {"M report error.category = Network W StopResourceWithError is called with "
       "is_network_error",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and record the start of a resource
         rum->StartView("my-view", "My View");
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And 2.4s later, we end that resource with an error
         clock.Tick(std::chrono::milliseconds(2400));
         rum->StopResourceWithError(
             "get-profile-123",
             "DNS lookup failed [CURLE_COULDNT_RESOLVE_HOST]",
             "6",
             "",
             true
         );
       },
       [](const nlohmann::json& events) {
         // Then an error event is sent to describe our finished resource
         REQUIRE(filter_events("resource", events).size() == 0);
         auto errors = filter_events("error", events);

         // And the error event has all expected error properties, plus relevant context
         // describing the resource, and its 'date' timestamp reflects error time, not
         // resource start time
         RequireEventMatch(errors[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "error",
            "date": 1700000002400,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View"
            },
            "error": {
              "resource": {
                "method": "GET",
                "url": "https://my-cool-website.biz/api/profile/123",
                "status_code": 0
              },
              "message": "DNS lookup failed [CURLE_COULDNT_RESOLVE_HOST]",
              "type": "6",
              "source": "network",
              "category": "Network"
            },
            "_dd": {
              "format_version": 2
            }
          })"));
       }},

      // === AddError() ===

      {"M send error event W AddError is called",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and then record an error at T+5ms
         rum->StartView("my-view", "My View");
         clock.TickMilliseconds(5);
         rum->AddError(
             RumErrorSource::Source,
             "Something went wrong",
             "AssertionError",
             "stack\ntrace"
         );
       },
       [](const nlohmann::json& events) {
         // Then we get an error event that contains our error details
         auto errors = filter_events("error", events);
         REQUIRE(errors.size() == 1);
         RequireEventMatch(errors[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "error",
            "date": 1700000000005,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View"
            },
            "error": {
              "message": "Something went wrong",
              "type": "AssertionError",
              "stack": "stack\ntrace",
              "source": "source",
              "category": "Exception"
            },
            "_dd": {
              "format_version": 2
            }
          })"));

         // And we also get a view event with an incremented error count
         auto views = filter_events("view", events);
         REQUIRE(views.size() == 2);
         REQUIRE(views[0]["view"]["id"] == errors[0]["view"]["id"]);
         REQUIRE(views[0]["date"] == 1700000000000);
         REQUIRE(views[0]["view"]["time_spent"] == 0);
         REQUIRE(views[0]["view"]["is_active"] == true);
         REQUIRE(views[0]["view"]["error"]["count"] == 0);
         REQUIRE(views[1]["view"]["id"] == errors[0]["view"]["id"]);
         REQUIRE(views[1]["date"] == 1700000000000);
         REQUIRE(views[1]["view"]["time_spent"] == 5000000);
         REQUIRE(views[1]["view"]["is_active"] == true);
         REQUIRE(views[1]["view"]["error"]["count"] == 1);
       }},

      {"M send error event with action.id W AddError is called with active action",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and a RUM action, and then record an error at
         // T+5ms
         rum->StartView("my-view", "My View");
         rum->AddAction(RumActionType::Click, "button1");
         clock.TickMilliseconds(5);
         rum->AddError(RumErrorSource::Console, "This is an error", "SomeErrorType");
       },
       [](const nlohmann::json& events) {
         // Then we get an error event that contains our error details, along with the
         // context for the active action
         auto errors = filter_events("error", events);
         REQUIRE(errors.size() == 1);
         RequireEventMatch(errors[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "error",
            "date": 1700000000005,
            "os": {
              "name": "MockOS",
              "version": "1.0.0",
              "build": "12345",
              "version_major": "1"
            },
            "device": {
              "type": "desktop",
              "name": "MockDevice",
              "model": "MockModel",
              "brand": "MockBrand",
              "architecture": "x86_64",
              "locale": "en-US",
              "time_zone": "UTC"
            },
            "application": {
              "id": "a991ca10-4004-4004-4004-beefbeefbeef"
            },
            "session": {
              "id": "${__NONZERO_UUID__}",
              "type": "user"
            },
            "view": {
              "id": "${__NONZERO_UUID__}",
              "url": "my-view",
              "name": "My View"
            },
            "action": {
              "id": "${__NONZERO_UUID__}"
            },
            "error": {
              "message": "This is an error",
              "type": "SomeErrorType",
              "source": "console",
              "category": "Exception"
            },
            "_dd": {
              "format_version": 2
            }
          })"));
       }},

      // === Action lifetime vis-a-vis resources ===

      {"M extend discrete action lifetime W concurrent resource remains active",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and add an action with a type other than custom
         rum->StartView("my-view", "My View");
         rum->AddAction(RumActionType::Click, "button1");

         // And then at T+50ms, a resource begins
         clock.Tick(std::chrono::milliseconds(50));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And then at T+150ms, the resource ends
         clock.Tick(std::chrono::milliseconds(100));
         rum->StopResource("get-profile-123", 200, 12345, RumResourceType::Xhr);
       },
       [](const nlohmann::json& events) {
         // Then:
         // - We have no error events
         REQUIRE(filter_events("error", events).size() == 0);

         // - Our action event is sent, its resource count is 1, and its duration is
         //   clamped at 100ms despite the fact that the scope persisted for 150ms
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["type"] == "click");
         REQUIRE(actions[0]["action"]["target"]["name"] == "button1");
         REQUIRE(!actions[0]["action"].contains("error"));
         REQUIRE(actions[0]["action"]["resource"]["count"] == 1);
         REQUIRE(actions[0]["action"]["loading_time"] == 100000000);

         // - We get a resource event, and its action.id matches the action event
         auto resources = filter_events("resource", events);
         REQUIRE(resources.size() == 1);
         REQUIRE(resources[0]["date"] == 1700000000050);
         REQUIRE(resources[0]["resource"]["duration"] == 100000000);
         REQUIRE(resources[0]["resource"]["method"] == "GET");
         REQUIRE(
             resources[0]["resource"]["url"] ==
             "https://my-cool-website.biz/api/profile/123"
         );
         REQUIRE(resources[0]["resource"]["size"] == 12345);
         REQUIRE(resources[0]["resource"]["status_code"] == 200);
         REQUIRE(resources[0]["resource"]["type"] == "xhr");
         REQUIRE(resources[0]["action"]["id"] == actions[0]["action"]["id"]);
       }},

      {"M extend continuous action lifetime W concurrent resource remains active",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start a RUM action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Scroll, "scroll1");

         // And then at T+9.8s, a resource begins
         clock.Tick(std::chrono::milliseconds(9800));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And then at T+14.8s, the resource ends
         clock.Tick(std::chrono::seconds(5));
         rum->StopResource("get-profile-123", 200, 12345, RumResourceType::Xhr);
       },
       [](const nlohmann::json& events) {
         // Then:
         // - We have no error events
         REQUIRE(filter_events("error", events).size() == 0);

         // - Our action event is sent, its resource count is 1, and its duration is
         //   clamped at 10s despite the fact that the scope persisted for 14.8s
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(!actions[0]["action"].contains("error"));
         REQUIRE(actions[0]["action"]["resource"]["count"] == 1);
         REQUIRE(actions[0]["action"]["loading_time"] == 10000000000);

         // - We get a resource event, and its action.id matches the action event
         auto resources = filter_events("resource", events);
         REQUIRE(resources.size() == 1);
         REQUIRE(resources[0]["date"] == 1700000009800);
         REQUIRE(resources[0]["resource"]["duration"] == 5000000000);
         REQUIRE(resources[0]["action"]["id"] == actions[0]["action"]["id"]);
       }},

      {"M stop action W pending resource is stopped due to error",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start a RUM action
         rum->StartView("my-view", "My View");
         rum->StartAction(RumActionType::Scroll, "scroll1");

         // And then at T+9.8s, a resource begins
         clock.Tick(std::chrono::milliseconds(9800));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And then at T+14.8s, the resource ends due to an error
         clock.Tick(std::chrono::seconds(5));
         rum->StopResourceWithError(
             "get-profile-123",
             "Invalid format",
             "ParseError",
             "stack-trace-here",
             false,
             200
         );
       },
       [](const nlohmann::json& events) {
         // Then:
         // - We have no resource events
         REQUIRE(filter_events("resource", events).size() == 0);

         // - Our action event is sent, its resource and error counts are both 1, and
         //   its duration is clamped at 10s despite the fact that the scope persisted
         //   for 14.8s
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["error"]["count"] == 1);
         REQUIRE(actions[0]["action"]["resource"]["count"] == 1);
         REQUIRE(actions[0]["action"]["loading_time"] == 10000000000);

         // - We get an error event describing the resource, and its action.id matches
         //   the action event
         auto errors = filter_events("error", events);
         REQUIRE(errors.size() == 1);
         REQUIRE(errors[0]["date"] == 1700000014800);
         REQUIRE(errors[0]["error"]["message"] == "Invalid format");
         REQUIRE(errors[0]["error"]["type"] == "ParseError");
         REQUIRE(errors[0]["error"]["stack"] == "stack-trace-here");
         REQUIRE(errors[0]["error"]["source"] == "network");
         REQUIRE(errors[0]["error"]["resource"]["method"] == "GET");
         REQUIRE(
             errors[0]["error"]["resource"]["url"] ==
             "https://my-cool-website.biz/api/profile/123"
         );
         REQUIRE(errors[0]["error"]["resource"]["status_code"] == 200);
         REQUIRE(errors[0]["action"]["id"] == actions[0]["action"]["id"]);
       }},

      {"M not extend action lifetime W resource is started after action timeout",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and add an action with a type other than custom
         rum->StartView("my-view", "My View");
         rum->AddAction(RumActionType::Click, "button1");

         // And then at T+150ms, a resource begins
         clock.Tick(std::chrono::milliseconds(150));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And then at T+200ms, the resource ends
         clock.Tick(std::chrono::milliseconds(50));
         rum->StopResource("get-profile-123", 200, 12345, RumResourceType::Xhr);
       },
       [](const nlohmann::json& events) {
         // Then:
         // - We have no error events
         REQUIRE(filter_events("error", events).size() == 0);

         // - Our action event is sent, its resource count is 0, and its duration is
         //   clamped at 100ms
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(!actions[0]["action"].contains("error"));
         REQUIRE(!actions[0]["action"].contains("resource"));
         REQUIRE(actions[0]["action"]["loading_time"] == 100000000);

         // - We get a resource event, and it has no action.id
         auto resources = filter_events("resource", events);
         REQUIRE(resources.size() == 1);
         REQUIRE(resources[0]["date"] == 1700000000150);
         REQUIRE(resources[0]["resource"]["duration"] == 50000000);
         REQUIRE(!resources[0].contains("action"));
       }},

      {"M continually extend action lifetime W concurrent resources overlap",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and add an action with a type other than custom
         rum->StartView("my-view", "My View");
         rum->AddAction(RumActionType::Click, "button1");

         // And then at T+50ms, a resource begins
         clock.Tick(std::chrono::milliseconds(50));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And then at T+150ms, another resource begins
         clock.Tick(std::chrono::milliseconds(100));
         rum->StartResource(
             "get-profile-456",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/456"
         );

         // And then at T+200ms, our first resource is stopped
         clock.Tick(std::chrono::milliseconds(50));
         rum->StopResource("get-profile-123", 200, 12345, RumResourceType::Xhr);
       },
       [](const nlohmann::json& events) {
         // Then:
         // - We have no error events
         REQUIRE(filter_events("error", events).size() == 0);

         // - Our action event is not sent because our second resource call is still
         //   pending
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 0);

         // - We get a single resource event, and it has a valid action.id
         auto resources = filter_events("resource", events);
         REQUIRE(resources.size() == 1);
         REQUIRE(resources[0]["date"] == 1700000000050);
         REQUIRE(resources[0]["resource"]["duration"] == 150000000);
         REQUIRE(resources[0]["action"]["id"].get_ref<const std::string&>() != "");
       }},

      {"M respect StopAction even W concurrent resource remains actives",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and add an action with a type other than custom
         rum->StartView("my-view", "My View");
         rum->AddAction(RumActionType::Click, "button1");

         // And then at T+50ms, a resource begins
         clock.Tick(std::chrono::milliseconds(50));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123"
         );

         // And then at T+70ms, we explicitly stop the action
         clock.Tick(std::chrono::milliseconds(20));
         rum->StopAction(RumActionType::Click, "button1");
       },
       [](const nlohmann::json& events) {
         // Then:
         // - We have no error events
         REQUIRE(filter_events("error", events).size() == 0);

         // - Our action event is sent, its duration is 70ms, and its resource count is
         //   zero
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(!actions[0]["action"].contains("error"));
         REQUIRE(!actions[0]["action"].contains("resource"));
         REQUIRE(actions[0]["action"]["loading_time"] == 70000000);

         // - We get no resource events
         auto resources = filter_events("resource", events);
         REQUIRE(resources.size() == 0);
       }},

      // === Action attributes, merging with view and global attributes ===

      {"M include action-level attributes as context W provided on start",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action with {"alpha":1,"bravo":2}
         rum->StartView("my-view", "My View");
         Attribute start_action_obj = Attribute::Object(2);
         start_action_obj.SetObjectProperty("alpha", Attribute::Int(1));
         start_action_obj.SetObjectProperty("bravo", Attribute::Int(2));
         rum->StartAction(RumActionType::Custom, "foo", start_action_obj);

         // And then at T+2s, we explicitly stop the action
         clock.Tick(std::chrono::seconds(2));
         rum->StopAction(RumActionType::Custom, "foo");
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our action has {"alpha":1,"bravo":2}
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["context"] == nlohmann::json{{"alpha", 1}, {"bravo", 2}});
       }},

      {"M merge command attributes into action-level attributes W command is "
       "StopAction",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action with {"alpha":1,"bravo":2}
         rum->StartView("my-view", "My View");
         Attribute start_action_obj = Attribute::Object(2);
         start_action_obj.SetObjectProperty("alpha", Attribute::Int(1));
         start_action_obj.SetObjectProperty("bravo", Attribute::Int(2));
         rum->StartAction(RumActionType::Custom, "foo", start_action_obj);

         // And then at T+2s, we stop the action with {"bravo":22,"charlie":33}
         clock.Tick(std::chrono::seconds(2));
         Attribute stop_action_obj = Attribute::Object(2);
         stop_action_obj.SetObjectProperty("bravo", Attribute::Int(22));
         stop_action_obj.SetObjectProperty("charlie", Attribute::Int(33));
         rum->StopAction(RumActionType::Custom, "foo", stop_action_obj);
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our action has
         // {"alpha":1,"bravo":22,"charlie":33}
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(
             actions[0]["context"] ==
             nlohmann::json{{"alpha", 1}, {"bravo", 22}, {"charlie", 33}}
         );
       }},

      {"M not merge command attributes into action-level attributes W command is not "
       "StopAction",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start an action with {"alpha":1,"bravo":2}
         rum->StartView("my-view", "My View");
         Attribute start_action_obj = Attribute::Object(2);
         start_action_obj.SetObjectProperty("alpha", Attribute::Int(1));
         start_action_obj.SetObjectProperty("bravo", Attribute::Int(2));
         rum->StartAction(RumActionType::Custom, "foo", start_action_obj);

         // And then at T+2s, we stop the _view_ with {"bravo":22,"charlie":33}
         clock.Tick(std::chrono::seconds(2));
         Attribute stop_view_obj = Attribute::Object(2);
         stop_view_obj.SetObjectProperty("bravo", Attribute::Int(22));
         stop_view_obj.SetObjectProperty("charlie", Attribute::Int(33));
         rum->StopView("my-view");
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our action has {"alpha":1,"bravo":2}: even
         // though the command that ends the action carries attributes, they are not
         // intended as action attributes, so they're ignored.
         // (...or should we have {"alpha":1,"bravo":2,"charlie":33}?)
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["context"] == nlohmann::json{{"alpha", 1}, {"bravo", 2}});
       }},

      {"M merge global <- view <- action attributes",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // Given global RUM attributes {"able":100,"baker":200}
         rum->AddAttribute("able", Attribute::Int(100));
         rum->AddAttribute("baker", Attribute::Int(200));

         // And a view with attributes {"baker":222,"charlie":333,"dog":444}
         Attribute start_view_obj = Attribute::Object(3);
         start_view_obj.SetObjectProperty("baker", Attribute::Int(222));
         start_view_obj.SetObjectProperty("charlie", Attribute::Int(333));
         start_view_obj.SetObjectProperty("dog", Attribute::Int(444));
         rum->StartView("my-view", "My View", start_view_obj);

         // When we start an action with {"alpha":1,"bravo":2,"dog":"good"}
         Attribute start_action_obj = Attribute::Object(3);
         start_action_obj.SetObjectProperty("alpha", Attribute::Int(1));
         start_action_obj.SetObjectProperty("bravo", Attribute::Int(2));
         start_action_obj.SetObjectProperty("dog", Attribute::String("good"));
         rum->StartAction(RumActionType::Custom, "foo", start_action_obj);

         // And then at T+2s, we explicitly stop the action
         clock.Tick(std::chrono::seconds(2));
         rum->StopAction(RumActionType::Custom, "foo");
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our action has
         // {"able":100,"baker":222,"charlie":333,"alpha":1,"bravo":2,"dog":"good"}
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(
             actions[0]["context"] == nlohmann::json{
                                          {"able", 100},
                                          {"baker", 222},
                                          {"charlie", 333},
                                          {"alpha", 1},
                                          {"bravo", 2},
                                          {"dog", "good"},
                                      }
         );
       }},

      // === Resource attributes, merging with view and global attributes ===

      {"M include resource-level attributes as context W provided on start",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start a resource with {"alpha":1,"bravo":2}
         rum->StartView("my-view", "My View");
         Attribute start_resource_obj = Attribute::Object(2);
         start_resource_obj.SetObjectProperty("alpha", Attribute::Int(1));
         start_resource_obj.SetObjectProperty("bravo", Attribute::Int(2));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123",
             start_resource_obj
         );

         // And then at T+2s, we stop the resource
         clock.Tick(std::chrono::seconds(2));
         rum->StopResource("get-profile-123", 200, 12345, RumResourceType::Xhr);
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our resource has {"alpha":1,"bravo":2}
         auto resources = filter_events("resource", events);
         REQUIRE(resources.size() == 1);
         REQUIRE(resources[0]["context"] == nlohmann::json{{"alpha", 1}, {"bravo", 2}});
       }},

      {"M merge attributes into resource-level attributes W StopResource is called "
       "with custom attributes",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start a resource with {"alpha":1,"bravo":2}
         rum->StartView("my-view", "My View");
         Attribute start_resource_obj = Attribute::Object(2);
         start_resource_obj.SetObjectProperty("alpha", Attribute::Int(1));
         start_resource_obj.SetObjectProperty("bravo", Attribute::Int(2));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123",
             start_resource_obj
         );

         // And then at T+2s, we stop the resource with {"bravo":22,"charlie":33}
         clock.Tick(std::chrono::seconds(2));
         Attribute stop_resource_obj = Attribute::Object(2);
         stop_resource_obj.SetObjectProperty("bravo", Attribute::Int(22));
         stop_resource_obj.SetObjectProperty("charlie", Attribute::Int(33));
         rum->StopResource(
             "get-profile-123", 200, 12345, RumResourceType::Xhr, stop_resource_obj
         );
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our resource has
         // {"alpha":1,"bravo":22,"charlie":33}
         auto resources = filter_events("resource", events);
         REQUIRE(resources.size() == 1);
         REQUIRE(
             resources[0]["context"] ==
             nlohmann::json{{"alpha", 1}, {"bravo", 22}, {"charlie", 33}}
         );
       }},

      {"M merge attributes into resource-level attributes W StopResourceWithError is "
       "called with custom attributes",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and start a resource with {"alpha":1,"bravo":2}
         rum->StartView("my-view", "My View");
         Attribute start_resource_obj = Attribute::Object(2);
         start_resource_obj.SetObjectProperty("alpha", Attribute::Int(1));
         start_resource_obj.SetObjectProperty("bravo", Attribute::Int(2));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123",
             start_resource_obj
         );

         // And then at T+2s, we stop the resource with {"bravo":22,"charlie":33}
         clock.Tick(std::chrono::seconds(2));
         Attribute stop_resource_obj = Attribute::Object(2);
         stop_resource_obj.SetObjectProperty("bravo", Attribute::Int(22));
         stop_resource_obj.SetObjectProperty("charlie", Attribute::Int(33));
         rum->StopResourceWithError(
             "get-profile-123",
             "Invalid format",
             "ParseError",
             "this\nis\na\nstack\ntrace\n",
             false,
             200,
             stop_resource_obj
         );
       },
       [](const nlohmann::json& events) {
         // Then the resulting RUM error event has {"alpha":1,"bravo":22,"charlie":33}
         auto errors = filter_events("error", events);
         REQUIRE(errors.size() == 1);
         REQUIRE(
             errors[0]["context"] ==
             nlohmann::json{{"alpha", 1}, {"bravo", 22}, {"charlie", 33}}
         );
       }},

      {"M merge global <- view <- resource attributes",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // Given global RUM attributes {"able":100,"baker":200}
         rum->AddAttribute("able", Attribute::Int(100));
         rum->AddAttribute("baker", Attribute::Int(200));

         // And a view with attributes {"baker":222,"charlie":333,"dog":444}
         Attribute start_view_obj = Attribute::Object(3);
         start_view_obj.SetObjectProperty("baker", Attribute::Int(222));
         start_view_obj.SetObjectProperty("charlie", Attribute::Int(333));
         start_view_obj.SetObjectProperty("dog", Attribute::Int(444));
         rum->StartView("my-view", "My View", start_view_obj);

         // When we start a resource with {"alpha":1,"bravo":2,"dog":"good"}
         Attribute start_resource_obj = Attribute::Object(3);
         start_resource_obj.SetObjectProperty("alpha", Attribute::Int(1));
         start_resource_obj.SetObjectProperty("bravo", Attribute::Int(2));
         start_resource_obj.SetObjectProperty("dog", Attribute::String("good"));
         rum->StartResource(
             "get-profile-123",
             RumResourceMethod::Get,
             "https://my-cool-website.biz/api/profile/123",
             start_resource_obj
         );

         // And then at T+2s, we stop the resource
         clock.Tick(std::chrono::seconds(2));
         rum->StopResource("get-profile-123", 200, 12345, RumResourceType::Xhr);
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our resource has
         // {"able":100,"baker":222,"charlie":333,"alpha":1,"bravo":2,"dog":"good"}
         auto resources = filter_events("resource", events);
         REQUIRE(resources.size() == 1);
         REQUIRE(
             resources[0]["context"] == nlohmann::json{
                                            {"able", 100},
                                            {"baker", 222},
                                            {"charlie", 333},
                                            {"alpha", 1},
                                            {"bravo", 2},
                                            {"dog", "good"},
                                        }
         );
       }},

      // === Error attributes ===

      {"M include custom attributes as context W provided on AddError",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we create a RUM view and add an error with {"alpha":1,"bravo":2}
         rum->StartView("my-view", "My View");
         Attribute add_error_obj = Attribute::Object(2);
         add_error_obj.SetObjectProperty("alpha", Attribute::Int(1));
         add_error_obj.SetObjectProperty("bravo", Attribute::Int(2));
         rum->AddError(
             RumErrorSource::Source,
             "Something went wrong",
             "AssertionError",
             "stack\ntrace",
             add_error_obj
         );
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our error has {"alpha":1,"bravo":2}
         auto errors = filter_events("error", events);
         REQUIRE(errors.size() == 1);
         REQUIRE(errors[0]["context"] == nlohmann::json{{"alpha", 1}, {"bravo", 2}});
       }},

      {"M merge global <- view <- error attributes",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // Given global RUM attributes {"able":100,"baker":200}
         rum->AddAttribute("able", Attribute::Int(100));
         rum->AddAttribute("baker", Attribute::Int(200));

         // And a view with attributes {"baker":222,"charlie":333,"dog":444}
         Attribute start_view_obj = Attribute::Object(3);
         start_view_obj.SetObjectProperty("baker", Attribute::Int(222));
         start_view_obj.SetObjectProperty("charlie", Attribute::Int(333));
         start_view_obj.SetObjectProperty("dog", Attribute::Int(444));
         rum->StartView("my-view", "My View", start_view_obj);

         // When we add an error with {"alpha":1,"bravo":2,"dog":"good"}
         Attribute add_error_obj = Attribute::Object(3);
         add_error_obj.SetObjectProperty("alpha", Attribute::Int(1));
         add_error_obj.SetObjectProperty("bravo", Attribute::Int(2));
         add_error_obj.SetObjectProperty("dog", Attribute::String("good"));
         rum->AddError(
             RumErrorSource::Source,
             "Something went wrong",
             "AssertionError",
             "stack\ntrace",
             add_error_obj
         );
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our error has
         // {"able":100,"baker":222,"charlie":333,"alpha":1,"bravo":2,"dog":"good"}
         auto errors = filter_events("error", events);
         REQUIRE(errors.size() == 1);
         REQUIRE(
             errors[0]["context"] == nlohmann::json{
                                         {"able", 100},
                                         {"baker", 222},
                                         {"charlie", 333},
                                         {"alpha", 1},
                                         {"bravo", 2},
                                         {"dog", "good"},
                                     }
         );
       }},

      // === Operations ===

      {"M emit start and end vital events W operation succeeds",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         rum->StartView("my-view", "My View");
         rum->StartOperation("checkout");
         clock.Tick(std::chrono::milliseconds(500));
         rum->SucceedOperation("checkout");
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 2);

         RequireEventMatch(vitals[0], DATADOG_RUM_EVENT_LITERAL(R"({
           "type": "vital",
           "date": 1700000000000,
           "os": {
             "name": "MockOS",
             "version": "1.0.0",
             "build": "12345",
             "version_major": "1"
           },
           "device": {
             "type": "desktop",
             "name": "MockDevice",
             "model": "MockModel",
             "brand": "MockBrand",
             "architecture": "x86_64",
             "locale": "en-US",
             "time_zone": "UTC"
           },
           "application": {"id": "a991ca10-4004-4004-4004-beefbeefbeef"},
           "session": {"id": "${__NONZERO_UUID__}", "type": "user"},
           "view": {
             "id": "${__NONZERO_UUID__}",
             "url": "my-view",
             "name": "My View"
           },
           "vital": {
             "name": "checkout",
             "type": "operation_step",
             "step_type": "start",
             "id": "${__NONZERO_UUID__}"
           },
           "_dd": {"format_version": 2}
         })"));

         RequireEventMatch(vitals[1], DATADOG_RUM_EVENT_LITERAL(R"({
           "type": "vital",
           "date": 1700000000500,
           "os": {
             "name": "MockOS",
             "version": "1.0.0",
             "build": "12345",
             "version_major": "1"
           },
           "device": {
             "type": "desktop",
             "name": "MockDevice",
             "model": "MockModel",
             "brand": "MockBrand",
             "architecture": "x86_64",
             "locale": "en-US",
             "time_zone": "UTC"
           },
           "application": {"id": "a991ca10-4004-4004-4004-beefbeefbeef"},
           "session": {"id": "${__NONZERO_UUID__}", "type": "user"},
           "view": {
             "id": "${__NONZERO_UUID__}",
             "url": "my-view",
             "name": "My View"
           },
           "vital": {
             "name": "checkout",
             "type": "operation_step",
             "step_type": "end",
             "id": "${__NONZERO_UUID__}"
           },
           "_dd": {"format_version": 2}
         })"));

         REQUIRE(vitals[1]["vital"].count("failure_reason") == 0);
       }},

      {"M emit end vital with failure_reason W operation fails with error",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         rum->StartView("my-view", "My View");
         rum->StartOperation("upload");
         rum->FailOperation("upload", RumOperationFailureReason::Error);
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 2);

         RequireEventMatch(vitals[1], DATADOG_RUM_EVENT_LITERAL(R"({
           "type": "vital",
           "date": 1700000000000,
           "os": {
             "name": "MockOS",
             "version": "1.0.0",
             "build": "12345",
             "version_major": "1"
           },
           "device": {
             "type": "desktop",
             "name": "MockDevice",
             "model": "MockModel",
             "brand": "MockBrand",
             "architecture": "x86_64",
             "locale": "en-US",
             "time_zone": "UTC"
           },
           "application": {"id": "a991ca10-4004-4004-4004-beefbeefbeef"},
           "session": {"id": "${__NONZERO_UUID__}", "type": "user"},
           "view": {
             "id": "${__NONZERO_UUID__}",
             "url": "my-view",
             "name": "My View"
           },
           "vital": {
             "name": "upload",
             "type": "operation_step",
             "step_type": "end",
             "id": "${__NONZERO_UUID__}",
             "failure_reason": "error"
           },
           "_dd": {"format_version": 2}
         })"));
       }},

      {"M include operation_key in vital events W operation started with key",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         rum->StartView("my-view", "My View");
         rum->StartOperation("checkout", "cart-42");
         rum->SucceedOperation("checkout", "cart-42");
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 2);

         RequireEventMatch(vitals[0], DATADOG_RUM_EVENT_LITERAL(R"({
           "type": "vital",
           "date": 1700000000000,
           "os": {
             "name": "MockOS",
             "version": "1.0.0",
             "build": "12345",
             "version_major": "1"
           },
           "device": {
             "type": "desktop",
             "name": "MockDevice",
             "model": "MockModel",
             "brand": "MockBrand",
             "architecture": "x86_64",
             "locale": "en-US",
             "time_zone": "UTC"
           },
           "application": {"id": "a991ca10-4004-4004-4004-beefbeefbeef"},
           "session": {"id": "${__NONZERO_UUID__}", "type": "user"},
           "view": {
             "id": "${__NONZERO_UUID__}",
             "url": "my-view",
             "name": "My View"
           },
           "vital": {
             "name": "checkout",
             "type": "operation_step",
             "step_type": "start",
             "id": "${__NONZERO_UUID__}",
             "operation_key": "cart-42"
           },
           "_dd": {"format_version": 2}
         })"));

         RequireEventMatch(vitals[1], DATADOG_RUM_EVENT_LITERAL(R"({
           "type": "vital",
           "date": 1700000000000,
           "os": {
             "name": "MockOS",
             "version": "1.0.0",
             "build": "12345",
             "version_major": "1"
           },
           "device": {
             "type": "desktop",
             "name": "MockDevice",
             "model": "MockModel",
             "brand": "MockBrand",
             "architecture": "x86_64",
             "locale": "en-US",
             "time_zone": "UTC"
           },
           "application": {"id": "a991ca10-4004-4004-4004-beefbeefbeef"},
           "session": {"id": "${__NONZERO_UUID__}", "type": "user"},
           "view": {
             "id": "${__NONZERO_UUID__}",
             "url": "my-view",
             "name": "My View"
           },
           "vital": {
             "name": "checkout",
             "type": "operation_step",
             "step_type": "end",
             "id": "${__NONZERO_UUID__}",
             "operation_key": "cart-42"
           },
           "_dd": {"format_version": 2}
         })"));
       }},

      // Attribute merging tests for operations
      {"M include command attributes in start vital W StartOperation with "
       "attributes",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         rum->StartView("my-view", "My View");
         Attribute attrs = Attribute::Object(2);
         attrs.SetObjectProperty("checkout.cart_id", Attribute::String("cart-123"));
         attrs.SetObjectProperty("checkout.item_count", Attribute::Int(3));
         rum->StartOperation("checkout", "", attrs);
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 1);
         REQUIRE(vitals[0]["vital"]["step_type"] == "start");
         REQUIRE(
             vitals[0]["context"] ==
             nlohmann::json{
                 {"checkout.cart_id", "cart-123"}, {"checkout.item_count", 3}
             }
         );
       }},

      {"M include command attributes in end vital W SucceedOperation with "
       "attributes",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         rum->StartView("my-view", "My View");
         rum->StartOperation("upload");
         Attribute attrs = Attribute::Object(1);
         attrs.SetObjectProperty("upload.bytes", Attribute::Int(1024000));
         rum->SucceedOperation("upload", "", attrs);
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 2);
         REQUIRE(vitals[1]["vital"]["step_type"] == "end");
         REQUIRE(vitals[1]["context"] == nlohmann::json{{"upload.bytes", 1024000}});
       }},

      {"M include command attributes in end vital W FailOperation with "
       "attributes",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         rum->StartView("my-view", "My View");
         rum->StartOperation("login");
         Attribute attrs = Attribute::Object(2);
         attrs.SetObjectProperty("error.code", Attribute::String("INVALID_CREDS"));
         attrs.SetObjectProperty("attempt.count", Attribute::Int(3));
         rum->FailOperation(
             "login", RumOperationFailureReason::Error, "", attrs
         );
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 2);
         REQUIRE(vitals[1]["vital"]["step_type"] == "end");
         REQUIRE(vitals[1]["vital"]["failure_reason"] == "error");
         REQUIRE(
             vitals[1]["context"] ==
             nlohmann::json{{"error.code", "INVALID_CREDS"}, {"attempt.count", 3}}
         );
       }},

      {"M merge global <- view <- command attributes in vital events",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // Global: {"able":100, "baker":200}
         rum->AddAttribute("able", Attribute::Int(100));
         rum->AddAttribute("baker", Attribute::Int(200));

         // View: {"baker":222, "charlie":333, "dog":444}
         Attribute view_attrs = Attribute::Object(3);
         view_attrs.SetObjectProperty("baker", Attribute::Int(222));  // shadows global
         view_attrs.SetObjectProperty("charlie", Attribute::Int(333));
         view_attrs.SetObjectProperty("dog", Attribute::Int(444));
         rum->StartView("my-view", "My View", view_attrs);

         // Operation: {"alpha":1, "bravo":2, "dog":"good"}
         Attribute op_attrs = Attribute::Object(3);
         op_attrs.SetObjectProperty("alpha", Attribute::Int(1));
         op_attrs.SetObjectProperty("bravo", Attribute::Int(2));
         op_attrs.SetObjectProperty("dog", Attribute::String("good"));  // shadows view
         rum->StartOperation("checkout", "", op_attrs);
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 1);
         // Result: global + view + operation, with operation > view > global precedence
         REQUIRE(
             vitals[0]["context"] == nlohmann::json{
                                         {"able", 100},   // from global
                                         {"baker", 222},  // from view (shadowed global)
                                         {"charlie", 333},  // from view
                                         {"alpha", 1},      // from operation
                                         {"bravo", 2},      // from operation
                                         {"dog", "good"}
                                         // from operation (shadowed view and global)
                                     }
         );
       }},

      {"M merge global <- command attributes W operation emitted without active view",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // Global: {"env":"production", "version":"1.2.3"}
         rum->AddAttribute("env", Attribute::String("production"));
         rum->AddAttribute("version", Attribute::String("1.2.3"));

         // No view started - operation runs in background
         Attribute op_attrs = Attribute::Object(2);
         op_attrs.SetObjectProperty("task.name", Attribute::String("sync"));
         op_attrs.SetObjectProperty(
             "env", Attribute::String("staging")
         );  // shadows global
         rum->StartOperation("background-sync", "", op_attrs);
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 1);
         REQUIRE(vitals[0]["view"]["id"] == "00000000-0000-0000-0000-000000000000");
         REQUIRE(
             vitals[0]["context"] ==
             nlohmann::json{
                 {"env", "staging"},    // from operation (shadowed global)
                 {"version", "1.2.3"},  // from global
                 {"task.name", "sync"}  // from operation
             }
         );
       }},

      {"M not merge StartOperation and SucceedOperation attributes "
       "together",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         rum->StartView("my-view", "My View");

         // StartOperation with {"start.timestamp":"2024-01-01"}
         Attribute start_attrs = Attribute::Object(1);
         start_attrs.SetObjectProperty(
             "start.timestamp", Attribute::String("2024-01-01")
         );
         rum->StartOperation("upload", "", start_attrs);

         // SucceedOperation with {"end.timestamp":"2024-01-02", "bytes":5000}
         Attribute succeed_attrs = Attribute::Object(2);
         succeed_attrs.SetObjectProperty(
             "end.timestamp", Attribute::String("2024-01-02")
         );
         succeed_attrs.SetObjectProperty("bytes", Attribute::Int(5000));
         rum->SucceedOperation("upload", "", succeed_attrs);
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 2);

         // Start event has only start attributes
         REQUIRE(vitals[0]["vital"]["step_type"] == "start");
         REQUIRE(
             vitals[0]["context"] == nlohmann::json{{"start.timestamp", "2024-01-01"}}
         );

         // End event has only end attributes (not merged with start)
         REQUIRE(vitals[1]["vital"]["step_type"] == "end");
         REQUIRE(
             vitals[1]["context"] ==
             nlohmann::json{{"end.timestamp", "2024-01-02"}, {"bytes", 5000}}
         );
       }},

      {"M omit context field in vital event W no attributes provided",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         rum->StartView("my-view", "My View");
         rum->StartOperation("checkout");    // no attributes
         rum->SucceedOperation("checkout");  // no attributes
       },
       [](const nlohmann::json& events) {
         auto vitals = filter_events("vital", events);
         REQUIRE(vitals.size() == 2);
         REQUIRE(vitals[0].count("context") == 0);
         REQUIRE(vitals[1].count("context") == 0);
       }},
  };
  for (const auto& tt : tests) {
    DYNAMIC_SECTION(tt.name) {
      // Given a RUM config modified as our test case demands
      RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");
      tt.config_func(config);

      // And a started SDK with RUM initialized from that config
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      auto core = CoreTestHarness::WrapForCpp(test);
      auto rum = Rum::Register(core, config);
      REQUIRE(rum);
      REQUIRE(core->Start());

      // When we perform the actions in our test case's callback
      tt.func(rum, test.clock);

      // And stop the core to flush all events and HTTP requests
      core->Stop();

      // Then RUM sends 0 or more JSON events
      nlohmann::json events;
      if (!test.client.requests.empty()) {
        events = MergeJsonArrays(test.client.requests);
      }

      // And the assertions in our test case's assert callback hold true
      tt.assert_func(events);
    }
  }
}
