// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string_view>
#include <vector>

#include "datadog/rum.hpp"
#include "support/core.hpp"
#include "support/json.hpp"

using namespace datadog;

TEST_CASE("Rum null safety", "[unit][rum][cpp-api]") {
  SECTION("M safely do nothing W this wraps nullptr") {
    // Given both a valid Core interface that has no valid implementation pointer, as
    // well as a straight-up null pointer to a Core interface
    const datadog::CoreConfig invalid_config("", "", "");
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
      rum->SetAttribute("foo", Attribute::Int(100));
      rum->DeleteAttribute("foo");

      rum->StopSession();

      Attribute attributes = Attribute::Object(1);
      attributes.SetObjectProperty("bar", Attribute::Int(100));
      rum->StartView("foo", "My View", attributes);
      rum->StopView("foo", attributes);

      // TODO(RUM-12322): Exercise RUM View Attributes API
      // TODO(RUM-11369): Exercise RUM Action API
      // TODO(RUM-12201): Exercise RUM Error API
      // TODO(RUM-12202): Exercise RUM Resource API
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
    // TODO: Surface some indication of whether a call to the C++ API succeeded (and
    // gave you a valid, functional object) or failed (and gave you a no-op interface)
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
    rum->SetAttribute("attr1", Attribute::Int(100));
    rum->StartView("bar", "Bar");
    rum->StopView("bar");
    rum->StopSession();
    rum->StartView("foo", "Foo");
    // TODO(RUM-12322): Exercise RUM View Attributes API
    // TODO(RUM-11369): Exercise RUM Action API
    // TODO(RUM-12201): Exercise RUM Error API
    // TODO(RUM-12202): Exercise RUM Resource API
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

TEST_CASE("Rum view events", "[unit][rum][cpp-api]") {
  struct TestParams {
    std::string_view name;
    std::function<void(RumConfig&)> config_func;
    std::function<void(std::shared_ptr<Rum>&, MockClock&)> func;
    std::function<void(const std::vector<std::string>&)> assert_func;
  };
  std::vector<TestParams> tests = {
      {"M send initial view event W new view is started",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we create a RUM view
         rum->StartView("my-view", "My View");
       },
       [](const std::vector<std::string>& events) {
         // Then RUM produces exactly one view event
         REQUIRE(events.size() == 1);
         REQUIRE(events[0] == "{\"placeholder-for\":\"view\"}");
       }},

      {"M send final view event W view is stopped",
       [](RumConfig&) {
         // Given an ordinary RUM config
       },
       [](std::shared_ptr<Rum>& rum, MockClock& clock) {
         // When we create a RUM view and then later stop it
         rum->StartView("my-view", "View");
         clock.Tick(std::chrono::seconds(15));
         rum->StopView("my-view");
       },
       [](const std::vector<std::string>& events) {
         // Then RUM produces a view event on start and stop
         REQUIRE(events.size() == 2);
         REQUIRE(events[0] == "{\"placeholder-for\":\"view\"}");
         REQUIRE(events[1] == "{\"placeholder-for\":\"view\"}");
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
       [](const std::vector<std::string>& events) {
         // Then RUM produces three events: a start and stop for 'my-view', and a start
         // for 'my-other-view'
         REQUIRE(events.size() == 3);
         REQUIRE(events[0] == "{\"placeholder-for\":\"view\"}");
         REQUIRE(events[1] == "{\"placeholder-for\":\"view\"}");
         REQUIRE(events[2] == "{\"placeholder-for\":\"view\"}");
       }},

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
       [](const std::vector<std::string>& events) {
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
       [](const std::vector<std::string>& events) {
         // Then RUM produces roughly 1320 events: 2 for each sampled session, with
         // approximately 660 sessions sampled
         REQUIRE(events.size() > 1320 - 200);
         REQUIRE(events.size() < 1320 + 200);
       }}

      // TODO(RUM-12321): Validate that view functions result in the expected events
      // TODO(RUM-12322): Validate that events include global attribute values
      // TODO(RUM-12322): Validate that events include view attribute values
      // TODO(RUM-11369): Validate that action functions result in the expected events
      // TODO(RUM-12201): Validate that error functions result in the expected events
      // TODO(RUM-12202): Validate that resource functions result in the expected events
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
      std::vector<std::string> events;
      if (!test.client.requests.empty()) {
        events = ParseJsonArrays(test.client.requests);
      }

      // And the assertions in our test case's assert callback hold true
      tt.assert_func(events);
    }
  }
}
