// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <functional>
#include <string_view>
#include <vector>

#include "datadog/rum.h"
#include "support/core.hpp"
#include "support/json_validation.hpp"

using namespace datadog;

TEST_CASE("dd_rum null safety", "[unit][rum][c-api]") {
  SECTION("M safely do nothing W target object is null") {
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_t obj = dd_attribute_object(1);
    dd_attribute_object_property_set(&obj, "bar", &int_100);

    dd_rum_config_init(nullptr, "a991ca10-4004-4004-4004-beefbeefbeef");
    dd_rum_config_set_application_id(nullptr, "a991ca10-4004-4004-4004-beefbeefbeef");

    REQUIRE(dd_rum_init(nullptr, nullptr) == nullptr);
    dd_rum_destroy(nullptr);

    dd_rum_attribute_set(nullptr, "foo", &int_100);
    dd_rum_attribute_delete(nullptr, "foo");

    dd_rum_stop_session(nullptr);

    dd_rum_start_view(nullptr, "foo", "My View");
    dd_rum_start_view_obj(nullptr, "foo", "My View", &obj);
    dd_rum_stop_view(nullptr, "foo");
    dd_rum_stop_view_obj(nullptr, "foo", &obj);

    // TODO(RUM-12322): Exercise RUM View Attributes API
    // TODO(RUM-11369): Exercise RUM Action API
    // TODO(RUM-12201): Exercise RUM Error API
    // TODO(RUM-12202): Exercise RUM Resource API
  }
}

TEST_CASE("dd_rum_init", "[unit][rum][c-api]") {
  SECTION("M accept config W dd_rum_config_init was called") {
    // Given a valid core
    dd_core_config_t core_config;
    dd_core_config_init(&core_config, "my-client-token", "my-service", "my-env");
    dd_core_t* core = dd_core_create(&core_config);

    // And a config struct that's been initialized with the bare-minimum set of values
    dd_rum_config config;
    dd_rum_config_init(&config, "a991ca10-4004-4004-4004-beefbeefbeef");

    // When we attempt to register RUM with that config
    dd_rum_t* rum = dd_rum_init(core, &config);

    // Then we get a valid dd_rum_t
    REQUIRE(rum != nullptr);

    // Cleanup
    dd_rum_destroy(rum);
    dd_core_destroy(core);
  }

  SECTION("M accept config W version is 1") {
    // Given a valid core
    dd_core_config_t core_config;
    dd_core_config_init(&core_config, "my-client-token", "my-service", "my-env");
    dd_core_t* core = dd_core_create(&core_config);

    // And a config struct that's been initialized with the bare-minimum set of values
    dd_rum_config config;
    dd_rum_config_init(&config, "a991ca10-4004-4004-4004-beefbeefbeef");

    // When we explicitly set the struct version to 1
    config.version = 1;

    // When we attempt to register RUM with that config
    dd_rum_t* rum = dd_rum_init(core, &config);

    // Then we get a valid dd_rum_t, even in a future where RUM_CONFIG_VERSION has been
    // bumped and is no longer 1
    REQUIRE(rum != nullptr);

    // Cleanup
    dd_rum_destroy(rum);
    dd_core_destroy(core);
  }

  SECTION("M reject config W version not set") {
    // Given a valid core
    dd_core_config_t core_config;
    dd_core_config_init(&core_config, "my-client-token", "my-service", "my-env");
    dd_core_t* core = dd_core_create(&core_config);

    // And a config struct that's just zero-filled
    dd_rum_config config;
    std::memset(&config, 0, sizeof(config));

    // When we attempt to register RUM with that config
    dd_rum_t* rum = dd_rum_init(core, &config);

    // Then we get null
    REQUIRE(rum == nullptr);

    // Cleanup
    dd_core_destroy(core);
  }

  SECTION("M reject config W required value not set") {
    // Given either "" or null as our input value for application_id
    const char* application_ids[2] = {"", nullptr};
    for (const char* application_id : application_ids) {
      // Given a valid core
      dd_core_config_t core_config;
      dd_core_config_init(&core_config, "my-client-token", "my-service", "my-env");
      dd_core_t* core = dd_core_create(&core_config);

      // And a config struct that's initialized with our empty/zero application ID
      dd_rum_config config;
      dd_rum_config_init(&config, application_id);

      // When we attempt to register RUM with that config
      dd_rum_t* rum = dd_rum_init(core, &config);

      // Then we get null
      REQUIRE(rum == nullptr);

      // Cleanup
      dd_core_destroy(core);
    }
  }
}

TEST_CASE("dd_rum usage when SDK not running", "[unit][rum][c-api]") {
  // Given an ordinary RUM config
  dd_rum_config config;
  dd_rum_config_init(&config, "a991ca10-4004-4004-4004-beefbeefbeef");

  // And a started SDK with RUM initialized from that config
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  dd_core_t* core = CoreTestHarness::WrapForC(test);
  dd_rum_t* rum = dd_rum_init(core, &config);
  REQUIRE(rum);

  // And a series of RUM API calls that should be no-ops when the SDK isn't running
  auto test_func = [](dd_rum_t* rum) {
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_rum_start_view(rum, "foo", "Foo");
    dd_rum_attribute_set(rum, "attr1", &int_100);
    dd_rum_start_view(rum, "bar", "Bar");
    dd_rum_stop_view(rum, "bar");
    dd_rum_stop_session(rum);
    dd_rum_start_view(rum, "foo", "Foo");
    dd_attribute_free(&int_100);
    // TODO(RUM-12322): Exercise RUM View Attributes API
    // TODO(RUM-11369): Exercise RUM Action API
    // TODO(RUM-12201): Exercise RUM Error API
    // TODO(RUM-12202): Exercise RUM Resource API
  };

  SECTION("M be safe to call RUM API W SDK not yet started") {
    // When we make a bunch of RUM API calls prior to SDK initialization
    test_func(rum);

    // And then start and stop the SDK
    REQUIRE(dd_core_start(core));
    dd_core_stop(core);

    // Then no crashes occur, and no RUM events are produced
    REQUIRE(test.client.requests.empty());
  }

  SECTION("M be safe to call RUM API W SDK already stopped") {
    // When we start and then stop the SDK
    REQUIRE(dd_core_start(core));
    dd_core_stop(core);

    // And then make a bunch of RUM API calls after SDK shutdown
    test_func(rum);

    // And then restart and stop the SDK once more for good measure
    REQUIRE(dd_core_start(core));
    dd_core_stop(core);

    // Then no crashes occur, and no RUM events are produced
    REQUIRE(test.client.requests.empty());
  }

  // Cleanup
  dd_rum_destroy(rum);
  dd_core_destroy(core);
}

TEST_CASE("dd_rum view events", "[unit][rum][c-api]") {
  struct TestParams {
    std::string_view name;
    std::function<void(dd_rum_config_t*)> config_func;
    std::function<void(dd_rum_t*, MockClock&)> func;
    std::function<void(const std::vector<std::string>&)> assert_func;
  };
  std::vector<TestParams> tests = {
      {"M send initial view event W new view is started",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock&) {
         // When we create a RUM view
         dd_rum_start_view(rum, "my-view", "My View");
       },
       [](const std::vector<std::string>& events) {
         // Then RUM produces exactly one view event
         REQUIRE(events.size() == 1);
         REQUIRE(events[0] == "{\"placeholder-for\":\"view\"}");
       }},

      {"M send final view event W view is stopped",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and then later stop it
         dd_rum_start_view(rum, "my-view", "My View");
         clock.Tick(std::chrono::seconds(15));
         dd_rum_stop_view(rum, "my-view");
       },
       [](const std::vector<std::string>& events) {
         // Then RUM produces a view event on start and stop
         REQUIRE(events.size() == 2);
         REQUIRE(events[0] == "{\"placeholder-for\":\"view\"}");
         REQUIRE(events[1] == "{\"placeholder-for\":\"view\"}");
       }},

      {"M send final + initial event W new view replaces previous view",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and then later replace it with a different view
         dd_rum_start_view(rum, "my-view", "My View");
         clock.Tick(std::chrono::seconds(15));
         dd_rum_start_view(rum, "my-other-view", "My Other View");
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
       [](dd_rum_config_t*) {
         // Given a RUM config with the default sample rate of 100%
       },
       [](dd_rum_t* rum, MockClock&) {
         // When we start and stop 100 views
         for (int i = 0; i < 100; i++) {
           std::string view_key = "view-" + std::to_string(i);
           dd_rum_start_view(rum, view_key.c_str(), "");
           dd_rum_stop_view(rum, view_key.c_str());
         }
       },
       [](const std::vector<std::string>& events) {
         // Then RUM sends exactly 200 view events: a start and a stop for each
         REQUIRE(events.size() == 200);
       }},

      {"M send roughly 2/3 of view events W session sample rate is 66%",
       [](dd_rum_config_t* config) {
         // Given a RUM config with a 66% session sample rate
         dd_rum_config_set_session_sample_rate(config, 66.0f);
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create 1000 views, using StopSession (which will emit a final view
         // event) to ensure that we create an independent session for each view
         for (int i = 0; i < 1000; i++) {
           std::string view_key = "view-" + std::to_string(i);
           dd_rum_start_view(rum, view_key.c_str(), "");
           clock.Tick(std::chrono::seconds(1));
           dd_rum_stop_session(rum);
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
      dd_rum_config config;
      dd_rum_config_init(&config, "a991ca10-4004-4004-4004-beefbeefbeef");
      tt.config_func(&config);

      // And a started SDK with RUM initialized from that config
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      dd_core_t* core = CoreTestHarness::WrapForC(test);
      dd_rum_t* rum = dd_rum_init(core, &config);
      REQUIRE(rum);
      REQUIRE(dd_core_start(core));

      // When we perform the actions in our test case's callback
      tt.func(rum, test.clock);

      // And stop the core to flush all events and HTTP requests
      dd_core_stop(core);

      // Then RUM sends 0 or more JSON events
      std::vector<std::string> events;
      if (!test.client.requests.empty()) {
        events = ParseJsonArrays(test.client.requests);
      }

      // And the assertions in our test case's assert callback hold true
      tt.assert_func(events);

      // Cleanup
      dd_rum_destroy(rum);
      dd_core_destroy(core);
    }
  }
}
