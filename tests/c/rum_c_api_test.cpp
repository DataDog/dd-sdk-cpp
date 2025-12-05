// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <functional>
#include <string_view>
#include <vector>

#include "datadog/rum.h"
#include "support/core.hpp"
#include "support/json_serialization.hpp"
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

    dd_rum_add_attribute(nullptr, "foo", &int_100);
    dd_rum_remove_attribute(nullptr, "foo");

    dd_rum_stop_session(nullptr);

    dd_rum_start_view(nullptr, "foo", "My View");
    dd_rum_start_view_obj(nullptr, "foo", "My View", &obj);
    dd_rum_add_view_attribute(nullptr, "foo", "something", &int_100);
    dd_rum_remove_view_attribute(nullptr, "foo", "something");
    dd_rum_stop_view(nullptr, "foo");
    dd_rum_stop_view_obj(nullptr, "foo", &obj);

    dd_rum_add_action(nullptr, DD_RUM_ACTION_TYPE_CLICK, "Button", &obj);
    dd_rum_start_action(nullptr, DD_RUM_ACTION_TYPE_CLICK, "Button", &obj);
    dd_rum_stop_action(nullptr, DD_RUM_ACTION_TYPE_CLICK, "Button", &obj);

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
    dd_rum_add_attribute(rum, "attr1", &int_100);
    dd_rum_remove_attribute(rum, "attr1");
    dd_rum_start_view(rum, "bar", "Bar");
    dd_rum_add_view_attribute(rum, "bar", "attr2", &int_100);
    dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "action1", NULL);
    dd_rum_remove_view_attribute(rum, "bar", "attr2");
    dd_rum_stop_view(rum, "bar");
    dd_rum_stop_session(rum);
    dd_rum_start_view(rum, "foo", "Foo");
    dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);
    dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);
    dd_attribute_free(&int_100);
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

TEST_CASE("dd_rum argument validation", "[unit][rum][c-api]") {
  auto with_rum = [](dd_rum_config_t* config,
                     dd_core_t* core,
                     std::function<void(dd_rum_t*)> func) {
    dd_rum_t* rum = dd_rum_init(core, config);
    REQUIRE(rum);
    dd_core_start(core);
    func(rum);
    dd_core_stop(core);
    dd_rum_destroy(rum);
  };

  // Given a series of tests consisting of a set of API calls and the warnings and/or
  // errors we expect to get in response
  struct TestParams {
    std::string_view name;
    std::function<void(dd_rum_config_t*, dd_core_t*)> func;
    std::vector<std::string_view> want_warnings;
    std::vector<std::string_view> want_errors;
  };
  std::vector<TestParams> tests = {

      // === Basic usage with no errors/warnings expected ===

      {"M print no warnings or errors W used normally",
       [](dd_rum_config_t* config, dd_core_t* core) {
         // Register RUM and start the SDK
         dd_rum_t* rum = dd_rum_init(core, config);
         dd_core_start(core);

         // Add and remove global attributes
         dd_attribute_t int_val = dd_attribute_int(100);
         dd_rum_add_attribute(rum, "foo", &int_val);
         dd_attribute_set_int(&int_val, 200);
         dd_rum_add_attribute(rum, "bar", &int_val);
         dd_rum_remove_attribute(rum, "foo");

         // Start a view with attributes
         dd_attribute_t view_attributes = dd_attribute_object(1);
         dd_attribute_t str_val = dd_attribute_string("hello");
         dd_attribute_object_property_set(&view_attributes, "bar", &str_val);
         dd_rum_start_view_obj(rum, "my-view", "My View", &view_attributes);

         // Modify view attributes
         dd_attribute_set_string(&str_val, "world");
         dd_rum_add_view_attribute(rum, "my-view", "baz", &str_val);
         dd_rum_remove_view_attribute(rum, "my-view", "bar");

         // Stop the view
         dd_rum_stop_view(rum, "my-view");

         // Shut down the SDK
         dd_core_stop(core);
         dd_rum_destroy(rum);
         dd_attribute_free(&int_val);
         dd_attribute_free(&str_val);
       },
       // All of the above should complete with 0 warnings and 0 errors
       {},
       {}},

      // === dd_rum_init() ===

      {"M print init error W dd_rum_config_t not properly initialized",
       [](dd_rum_config_t* config, dd_core_t* core) {
         std::memset(config, 0, sizeof(dd_rum_config_t));
         dd_rum_init(core, config);
       },
       {},
       {"RUM initialization failed: dd_rum_config_t value must be initialized via "
        "dd_rum_config_init"}},

      {"M print init error W configured application_id is empty string",
       [](dd_rum_config_t* config, dd_core_t* core) {
         dd_rum_config_set_application_id(config, "");
         dd_rum_init(core, config);
       },
       {},
       {"RUM initialization failed: application_id value supplied via dd_rum_config_t "
        "must be a valid, nonzero UUID"}},

      {"M print init error W configured application_id is invalid UUID",
       [](dd_rum_config_t* config, dd_core_t* core) {
         dd_rum_config_set_application_id(config, "not-a-valid-uuid");
         dd_rum_init(core, config);
       },
       {},
       {"RUM initialization failed: application_id value supplied via dd_rum_config_t "
        "must be a valid, nonzero UUID"}},

      {"M print init error W configured application_id is nil UUID",
       [](dd_rum_config_t* config, dd_core_t* core) {
         dd_rum_config_set_application_id(
             config, "00000000-0000-0000-0000-000000000000"
         );
         dd_rum_init(core, config);
       },
       {},
       {"RUM initialization failed: application_id value supplied via dd_rum_config_t "
        "must be a valid, nonzero UUID"}},

      // === dd_rum_add_attribute() ===

      {"M print warning W dd_rum_add_attribute is called with NULL name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_attribute_t int_100 = dd_attribute_int(100);
           dd_rum_add_attribute(rum, nullptr, &int_100);
           dd_attribute_free(&int_100);
         });
       },
       {"dd_rum_add_attribute call ignored: application must supply an attribute name"},
       {}},

      {"M print warning W dd_rum_add_attribute is called with NULL value",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_add_attribute(rum, "foo", nullptr);
         });
       },
       {"dd_rum_add_attribute call ignored: application must supply an attribute "
        "value"},
       {}},

      // === dd_rum_remove_attribute() ===

      {"M print warning W dd_rum_remove_attribute is called with NULL name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_remove_attribute(rum, nullptr);
         });
       },
       {"dd_rum_remove_attribute call ignored: application must supply an attribute "
        "name"},
       {}},

      // === dd_rum_start_view() ===

      {"M print warning W dd_rum_start_view is called with NULL key",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, nullptr, "My View");
         });
       },
       {"dd_rum_start_view call ignored: application must supply a non-empty view key"},
       {}},

      {"M print warning W dd_rum_start_view is called with empty key",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "", "My View");
         });
       },
       {"dd_rum_start_view call ignored: application must supply a non-empty view key"},
       {}},

      {"M print no warning W dd_rum_start_view is called with NULL view name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", nullptr);
         });
       },
       {},
       {}},

      {"M print no warning W dd_rum_start_view is called with empty view name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "");
         });
       },
       {},
       {}},

      // === dd_rum_stop_view() ===

      {"M print warning W dd_rum_stop_view is called with NULL key",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) { dd_rum_stop_view(rum, nullptr); });
       },
       {"dd_rum_stop_view call ignored: application must supply a non-empty view key"},
       {}},

      {"M print warning W dd_rum_stop_view is called with empty key",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) { dd_rum_stop_view(rum, ""); });
       },
       {"dd_rum_stop_view call ignored: application must supply a non-empty view key"},
       {}},

      // === dd_rum_add_view_attribute() ===

      {"M print warning W dd_rum_add_view_attribute is called with NULL view key",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_attribute_t int_100 = dd_attribute_int(100);
           dd_rum_add_view_attribute(rum, NULL, "foo", &int_100);
           dd_attribute_free(&int_100);
         });
       },
       {"dd_rum_add_view_attribute call ignored: application must supply a non-empty "
        "view key"},
       {}},

      {"M print warning W dd_rum_add_view_attribute is called with empty view key",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_attribute_t int_100 = dd_attribute_int(100);
           dd_rum_add_view_attribute(rum, "", "foo", &int_100);
           dd_attribute_free(&int_100);
         });
       },
       {"dd_rum_add_view_attribute call ignored: application must supply a non-empty "
        "view key"},
       {}},

      {"M print warning W dd_rum_add_view_attribute is called with NULL attribute name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_attribute_t int_100 = dd_attribute_int(100);
           dd_rum_add_view_attribute(rum, "my-view", nullptr, &int_100);
           dd_attribute_free(&int_100);
         });
       },
       {"dd_rum_add_view_attribute call ignored: application must supply an attribute "
        "name"},
       {}},

      {"M print warning W dd_rum_add_view_attribute is called with NULL value",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_rum_add_view_attribute(rum, "my-view", "foo", nullptr);
         });
       },
       {"dd_rum_add_view_attribute call ignored: application must supply an attribute "
        "value"},
       {}},

      // === dd_rum_remove_view_attribute() ===

      {"M print warning W dd_rum_remove_view_attribute is called with NULL view key",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_rum_remove_view_attribute(rum, NULL, "foo");
         });
       },
       {"dd_rum_remove_view_attribute call ignored: application must supply a "
        "non-empty view key"},
       {}},

      {"M print warning W dd_rum_remove_view_attribute is called with empty view key",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_rum_remove_view_attribute(rum, "", "foo");
         });
       },
       {"dd_rum_remove_view_attribute call ignored: application must supply a "
        "non-empty view key"},
       {}},

      {"M print warning W dd_rum_remove_view_attribute is called with NULL name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_rum_remove_view_attribute(rum, "my-view", nullptr);
         });
       },
       {"dd_rum_remove_view_attribute call ignored: application must supply an "
        "attribute name"},
       {}},

      // === dd_rum_add_action() / dd_rum_start_action() ===

      {"M print warning W dd_rum_add_action is called with NULL name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, NULL, NULL);
         });
       },
       {"dd_rum_add_action call ignored: application must supply a non-empty action "
        "name"},
       {}},

      {"M print warning W dd_rum_add_action is called with empty name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "", NULL);
         });
       },
       {"dd_rum_add_action call ignored: application must supply a non-empty action "
        "name"},
       {}},

      {"M print warning W dd_rum_start_action is called with NULL name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, NULL, NULL);
         });
       },
       {"dd_rum_start_action call ignored: application must supply a non-empty action "
        "name"},
       {}},

      {"M print warning W dd_rum_start_action is called with empty name",
       [&](dd_rum_config_t* config, dd_core_t* core) {
         with_rum(config, core, [](dd_rum_t* rum) {
           dd_rum_start_view(rum, "my-view", "My View");
           dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "", NULL);
         });
       },
       {"dd_rum_start_action call ignored: application must supply a non-empty action "
        "name"},
       {}},
  };
  for (const auto& tt : tests) {
    DYNAMIC_SECTION(tt.name) {
      // Given a default RUM config and a core
      dd_rum_config config;
      dd_rum_config_init(&config, "a991ca10-4004-4004-4004-beefbeefbeef");
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      dd_core_t* core = CoreTestHarness::WrapForC(test);

      // When we execute our test function to exercise the RUM API
      tt.func(&config, core);

      // Then we get the expected set of diagnostic warnings and errors
      REQUIRE(test.cpp_diagnostics.size() == 0);
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
        return x.contains("type") && x["type"] == type;
      }
  );
  return result;
}

TEST_CASE("dd_rum events", "[unit][rum][c-api]") {
  struct TestParams {
    std::string_view name;
    std::function<void(dd_rum_config_t*)> config_func;
    std::function<void(dd_rum_t*, MockClock&)> func;
    std::function<void(const nlohmann::json&)> assert_func;
  };
  std::vector<TestParams> tests = {

      // === Basic view event validation ===

      {"M send initial view event W new view is started",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock&) {
         // When we create a RUM view
         dd_rum_start_view(rum, "my-view", "My View");
       },
       [](const nlohmann::json& events) {
         // Then RUM produces exactly one view event
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         RequireEventMatch(events[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "view",
            "date": 1700000000000,
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and then later stop it
         dd_rum_start_view(rum, "my-view", "My View");
         clock.Tick(std::chrono::seconds(15));
         dd_rum_stop_view(rum, "my-view");
       },
       [](const nlohmann::json& events) {
         // Then RUM produces two events
         REQUIRE(events.size() == 2);
         // And the first describes the state of the view upon creation
         RequireEventMatch(events[0], DATADOG_RUM_EVENT_LITERAL(R"({
            "type": "view",
            "date": 1700000000000,
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and then later replace it with a different view
         dd_rum_start_view(rum, "my-view", "My View");
         clock.Tick(std::chrono::seconds(15));
         dd_rum_start_view(rum, "my-other-view", "My Other View");
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
       [](const nlohmann::json& events) {
         // Then RUM sends exactly 200 view events: a start and a stop for each
         REQUIRE(events.size() == 200);
       }},

      // === Session sampling ===

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
       [](const nlohmann::json& events) {
         // Then RUM produces roughly 1320 events: 2 for each sampled session, with
         // approximately 660 sessions sampled
         REQUIRE(events.size() > 1320 - 200);
         REQUIRE(events.size() < 1320 + 200);
       }},

      // === Inclusion of view attributes via StartView/StopView ===

      {"M include view attributes in view event W set via dd_rum_start_view",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock&) {
         // When we start a new view with {"foo":100}
         dd_attribute_t start_view_attributes = dd_attribute_object(4);
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_attribute_object_property_set(&start_view_attributes, "foo", &int_100);
         dd_attribute_free(&int_100);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_attributes);
         dd_attribute_free(&start_view_attributes);
       },
       [](const nlohmann::json& events) {
         // Then our initial view event should have {"foo":100}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         REQUIRE(events[0]["context"] == nlohmann::json{{"foo", 100}});
       }},

      {"M isolate view attributes to target view W dd_rum_start_view starts new view",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         // When we start a new view "my-view" with {"foo":100}
         dd_attribute_t my_view_attributes = dd_attribute_object(4);
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_attribute_object_property_set(&my_view_attributes, "foo", &int_100);
         dd_attribute_free(&int_100);
         dd_rum_start_view_obj(rum, "my-view", "My View", &my_view_attributes);
         dd_attribute_free(&my_view_attributes);

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we start another view "other-view" with {"bar":200}
         dd_attribute_t other_view_attributes = dd_attribute_object(4);
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_attribute_object_property_set(&other_view_attributes, "bar", &int_200);
         dd_attribute_free(&int_200);
         dd_rum_start_view_obj(rum, "other-view", "Other View", &other_view_attributes);
         dd_attribute_free(&other_view_attributes);
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

      {"M isolate view attributes to target view W dd_rum_start_view starts new view "
       "{with identical key}",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         // When we start a new view "my-view" with {"foo":100}
         dd_attribute_t my_view_attributes = dd_attribute_object(4);
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_attribute_object_property_set(&my_view_attributes, "foo", &int_100);
         dd_attribute_free(&int_100);
         dd_rum_start_view_obj(rum, "my-view", "My View", &my_view_attributes);
         dd_attribute_free(&my_view_attributes);

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we start another view, also using the key "my-view", with {"bar":200}
         dd_attribute_t my_view_2_attributes = dd_attribute_object(4);
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_attribute_object_property_set(&my_view_2_attributes, "bar", &int_200);
         dd_attribute_free(&int_200);
         dd_rum_start_view_obj(rum, "my-view", "My View", &my_view_2_attributes);
         dd_attribute_free(&my_view_2_attributes);
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

      {"M include view attributes in view event W set via dd_rum_stop_view",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         // When we start a new view with {"foo":100,"bar":200}
         dd_attribute_t start_view_attributes = dd_attribute_object(4);
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_attribute_object_property_set(&start_view_attributes, "foo", &int_100);
         dd_attribute_object_property_set(&start_view_attributes, "bar", &int_200);
         dd_attribute_free(&int_100);
         dd_attribute_free(&int_200);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_attributes);
         dd_attribute_free(&start_view_attributes);

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we issue a StopView call for that same view, with {"bar":300,"baz":400}
         dd_attribute_t stop_view_attributes = dd_attribute_object(4);
         dd_attribute_t int_300 = dd_attribute_int(300);
         dd_attribute_t int_400 = dd_attribute_int(400);
         dd_attribute_object_property_set(&stop_view_attributes, "bar", &int_300);
         dd_attribute_object_property_set(&stop_view_attributes, "baz", &int_400);
         dd_attribute_free(&int_300);
         dd_attribute_free(&int_400);
         dd_rum_stop_view_obj(rum, "my-view", &stop_view_attributes);
         dd_attribute_free(&stop_view_attributes);
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

      {"M include view attributes in view event W set via dd_rum_add_view_attribute",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         // When we start a new view with no attributes
         dd_rum_start_view(rum, "my-view", "My View");

         // And we then set {"foo":100} on the view after its creation
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_rum_add_view_attribute(rum, "my-view", "foo", &int_100);
         dd_attribute_free(&int_100);

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we issue a StopView call to produce another view event
         dd_rum_stop_view(rum, "my-view");
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

      {"M modify existing view attribute value W dd_rum_add_view_attribute called for "
       "existing attribute",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         // When we start a new view with no attributes
         dd_rum_start_view(rum, "my-view", "My View");

         // And we then set {"foo":100} on the view after its creation
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_rum_add_view_attribute(rum, "my-view", "foo", &int_100);
         dd_attribute_free(&int_100);

         // And we then set {"foo":200} immediately thereafter
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_rum_add_view_attribute(rum, "my-view", "foo", &int_200);
         dd_attribute_free(&int_200);

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         dd_rum_stop_view(rum, "my-view");
       },
       [](const nlohmann::json& events) {
         // Then our final view event has {"foo":200}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);
         REQUIRE(events[1]["context"] == nlohmann::json{{"foo", 200}});
       }},

      {"M remove existing view attribute value W dd_rum_remove_view_attribute called "
       "for existing attribute",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         // When we start a new view with no attributes
         dd_rum_start_view(rum, "my-view", "My View");

         // And we then set {"foo":100,"bar":200} on the view after its creation
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_rum_add_view_attribute(rum, "my-view", "foo", &int_100);
         dd_rum_add_view_attribute(rum, "my-view", "bar", &int_200);
         dd_attribute_free(&int_100);
         dd_attribute_free(&int_200);

         // And we then delete "foo" immediately thereafter
         dd_rum_remove_view_attribute(rum, "my-view", "foo");

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         dd_rum_stop_view(rum, "my-view");
       },
       [](const nlohmann::json& events) {
         // Then our final view event has {"bar":200}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);
         REQUIRE(events[1]["context"] == nlohmann::json{{"bar", 200}});
       }},

      {"M do nothing W dd_rum_remove_view_attribute called for nonexistent attribute",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         // When we start a new view with no attributes
         dd_rum_start_view(rum, "my-view", "My View");

         // And we then attempt to delete an attribute called "foo", which doens't exist
         dd_rum_remove_view_attribute(rum, "my-view", "foo");

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         dd_rum_stop_view(rum, "my-view");
       },
       [](const nlohmann::json& events) {
         // Then our final view event has no user attributes
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 2);
         REQUIRE(!events[1].contains("context"));
       }},

      {"M do nothing W dd_rum_remove_view_attribute called for nonexistent view",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock&) {
         // When we attempt to remove a view attribute from a view that doesn't exist
         dd_rum_remove_view_attribute(rum, "nonexistent-view", "foo");
       },
       [](const nlohmann::json& events) {
         // Then nothing happens
         REQUIRE(events.is_null());
       }},

      {"M do nothing W dd_rum_add_view_attribute called for nonexistent view",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock&) {
         // When we attempt to add a view attribute to a view that doesn't exist
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_rum_add_view_attribute(rum, "nonexistent-view", "foo", &int_100);
         dd_attribute_free(&int_100);
       },
       [](const nlohmann::json& events) {
         // Then nothing happens
         REQUIRE(events.is_null());
       }},

      {"M mutate view attributes W start/attr/stop funcs are called successively",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         dd_attribute_t int_val = dd_attribute_int(0);

         // When we start a new view with {"able":1,"baker":2,"charlie":3,"dog":4}
         dd_attribute_t start_view_obj = dd_attribute_object(4);
         dd_attribute_set_int(&int_val, 1);
         dd_attribute_object_property_set(&start_view_obj, "able", &int_val);
         dd_attribute_set_int(&int_val, 2);
         dd_attribute_object_property_set(&start_view_obj, "baker", &int_val);
         dd_attribute_set_int(&int_val, 3);
         dd_attribute_object_property_set(&start_view_obj, "charlie", &int_val);
         dd_attribute_set_int(&int_val, 4);
         dd_attribute_object_property_set(&start_view_obj, "dog", &int_val);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_obj);
         dd_attribute_free(&start_view_obj);

         // And we then delete "baker" and set {"charlie":"modified"} and {"dog":444}
         dd_rum_remove_view_attribute(rum, "my-view", "baker");
         dd_attribute_t str_modified = dd_attribute_string("modified");
         dd_rum_add_view_attribute(rum, "my-view", "charlie", &str_modified);
         dd_attribute_free(&str_modified);
         dd_attribute_set_int(&int_val, 444);
         dd_rum_add_view_attribute(rum, "my-view", "dog", &int_val);

         // And 15 seconds passes
         clock.Tick(std::chrono::seconds(15));

         // And we then stop the view, supplying {"dog":98,"easy":99}
         dd_attribute_t stop_view_obj = dd_attribute_object(2);
         dd_attribute_set_int(&int_val, 98);
         dd_attribute_object_property_set(&stop_view_obj, "dog", &int_val);
         dd_attribute_set_int(&int_val, 99);
         dd_attribute_object_property_set(&stop_view_obj, "easy", &int_val);
         dd_rum_stop_view_obj(rum, "my-view", &stop_view_obj);
         dd_attribute_free(&stop_view_obj);

         // Cleanup
         dd_attribute_free(&int_val);
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

      {"M include global attributes in view events W dd_rum_add_attribute called",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock&) {
         // When we add a global attribute {"x":"hello"}
         dd_attribute_t str_hello = dd_attribute_string("hello");
         dd_rum_add_attribute(rum, "x", &str_hello);
         dd_attribute_free(&str_hello);

         // And we start a new view with {"foo":100}
         dd_attribute_t start_view_attributes = dd_attribute_object(4);
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_attribute_object_property_set(&start_view_attributes, "foo", &int_100);
         dd_attribute_free(&int_100);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_attributes);
         dd_attribute_free(&start_view_attributes);
       },
       [](const nlohmann::json& events) {
         // Then our initial view event should have {"x":"hello","foo":100}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         REQUIRE(events[0]["context"] == nlohmann::json{{"x", "hello"}, {"foo", 100}});
       }},

      {"M modify global attribute value W dd_rum_add_attribute called for existing "
       "attribute",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         // When we add a global attribute {"x":"hello"}
         dd_attribute_t str_hello = dd_attribute_string("hello");
         dd_rum_add_attribute(rum, "x", &str_hello);
         dd_attribute_free(&str_hello);

         // And we start a new view with {"foo":100}
         dd_attribute_t start_view_attributes = dd_attribute_object(4);
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_attribute_object_property_set(&start_view_attributes, "foo", &int_100);
         dd_attribute_free(&int_100);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_attributes);
         dd_attribute_free(&start_view_attributes);

         // And we update our global attribute to {"x":"world"}
         dd_attribute_t str_world = dd_attribute_string("world");
         dd_rum_add_attribute(rum, "x", &str_world);
         dd_attribute_free(&str_world);

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         dd_rum_stop_view(rum, "my-view");
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

      {"M remove global attribute value W dd_rum_remove_attribute called for existing "
       "attribute",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         // When we set global attributes {"x":"hello","y":"world"}
         dd_attribute_t str_hello = dd_attribute_string("hello");
         dd_attribute_t str_world = dd_attribute_string("world");
         dd_rum_add_attribute(rum, "x", &str_hello);
         dd_rum_add_attribute(rum, "y", &str_world);
         dd_attribute_free(&str_hello);
         dd_attribute_free(&str_world);

         // And we start a new view with {"foo":100}
         dd_attribute_t start_view_attributes = dd_attribute_object(4);
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_attribute_object_property_set(&start_view_attributes, "foo", &int_100);
         dd_attribute_free(&int_100);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_attributes);
         dd_attribute_free(&start_view_attributes);

         // And we then delete the global attribute "x"
         dd_rum_remove_attribute(rum, "x");

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         dd_rum_stop_view(rum, "my-view");
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

      {"M do nothing W dd_rum_remove_attribute called for nonexistent attribute",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock&) {
         // When we attempt to remove a global attribute that doesn't exist
         dd_rum_remove_attribute(rum, "foo");
       },
       [](const nlohmann::json& events) {
         // Then nothing happens
         REQUIRE(events.is_null());
       }},

      {"M merge view attributes into global attributes",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         dd_attribute_t int_val = dd_attribute_int(0);

         // When we add global attributes {"alpha":1,"bravo":2,"charlie":3}
         dd_attribute_set_int(&int_val, 1);
         dd_rum_add_attribute(rum, "alpha", &int_val);
         dd_attribute_set_int(&int_val, 2);
         dd_rum_add_attribute(rum, "bravo", &int_val);
         dd_attribute_set_int(&int_val, 3);
         dd_rum_add_attribute(rum, "charlie", &int_val);

         // And we start a new view with {"able":100,"baker":200,"charlie":300}
         dd_attribute_t start_view_obj = dd_attribute_object(3);
         dd_attribute_set_int(&int_val, 100);
         dd_attribute_object_property_set(&start_view_obj, "able", &int_val);
         dd_attribute_set_int(&int_val, 200);
         dd_attribute_object_property_set(&start_view_obj, "baker", &int_val);
         dd_attribute_set_int(&int_val, 300);
         dd_attribute_object_property_set(&start_view_obj, "charlie", &int_val);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_obj);
         dd_attribute_free(&start_view_obj);

         // And we then remove the global "alpha"
         dd_rum_remove_attribute(rum, "alpha");

         // And we update the global "bravo" to "modified"
         dd_attribute_t str_modified = dd_attribute_string("modified");
         dd_rum_add_attribute(rum, "bravo", &str_modified);
         dd_attribute_free(&str_modified);

         // And we remove the view-level "charlie"
         dd_rum_remove_view_attribute(rum, "my-view", "charlie");

         // And we stop the view 15 seconds later
         clock.Tick(std::chrono::seconds(15));
         dd_rum_stop_view(rum, "my-view");

         // Cleanup
         dd_attribute_free(&int_val);
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
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         dd_attribute_t int_val = dd_attribute_int(0);

         // When we set global attributes {"alpha":1,"bravo":2,"charlie":3}
         dd_attribute_set_int(&int_val, 1);
         dd_rum_add_attribute(rum, "alpha", &int_val);
         dd_attribute_set_int(&int_val, 2);
         dd_rum_add_attribute(rum, "bravo", &int_val);
         dd_attribute_set_int(&int_val, 3);
         dd_rum_add_attribute(rum, "charlie", &int_val);

         // And we start a view with {"able":100,"baker":200,"charlie":300}
         dd_attribute_t start_view_obj = dd_attribute_object(3);
         dd_attribute_set_int(&int_val, 100);
         dd_attribute_object_property_set(&start_view_obj, "able", &int_val);
         dd_attribute_set_int(&int_val, 200);
         dd_attribute_object_property_set(&start_view_obj, "baker", &int_val);
         dd_attribute_set_int(&int_val, 300);
         dd_attribute_object_property_set(&start_view_obj, "charlie", &int_val);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_obj);
         dd_attribute_free(&start_view_obj);

         // And we start a resource within the active view
         // TODO(RUM-12202): Call dd_rum_start_resource()

         // And 15 seconds later, we create a new view with the same key, thereby
         // stopping the original view while it still has pending resources
         clock.Tick(std::chrono::seconds(15));
         dd_rum_start_view(rum, "my-view", "My View");

         // And we modify global attributes after the original view has stopped,
         // deleting "alpha" and adding {"delta":4}
         dd_rum_remove_attribute(rum, "alpha");
         dd_attribute_set_int(&int_val, 4);
         dd_rum_add_attribute(rum, "delta", &int_val);

         // And we attempt to modify view attributes after the view has stopped, trying
         // to delete "able" and trying to add {"dog":400}
         dd_rum_remove_view_attribute(rum, "my-view", "able");
         dd_attribute_set_int(&int_val, 400);
         dd_rum_add_view_attribute(rum, "my-view", "dog", &int_val);

         // And 1 second later, we stop the resource, allowing our original view scope
         // to close
         clock.Tick(std::chrono::seconds(1));
         // TODO(RUM-12202): Call dd_rum_stop_resource()

         // And finally, 5 seconds after that, we close the new view
         clock.Tick(std::chrono::seconds(5));
         dd_rum_stop_view(rum, "my-view");

         // Cleanup
         dd_attribute_free(&int_val);
       },
       [](const nlohmann::json& events) {
         // Then we get the following sequence of RUM events:
         // - At T+0: a `view` with:
         //   - _dd.document_version: 0
         //   - is_active: true
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         // - At T+15: a `view` with:
         //   - _dd.document_version: 1
         //   - is_active: true
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         // - At T+15: a `view` with:
         //   - _dd.document_version: 0
         //   - is_active: true
         //   - context: {"alpha":1,"bravo":2,"charlie":3}
         // - At T+16: a `resource` recording successful completion, with:
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         // - At T+16: a `view` with:
         //   - _dd.document_version: 2
         //   - is_active: false
         //   - resource.count: 1
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         // - At T+21: a `view` with:
         //   - _dd.document_version: 1
         //   - is_active: false
         //   - context: {"bravo":2,"charlie":3,"dog":4}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 4);
         // TODO(RUM-12202): Implement the assertions described above
       }},

      // === Retention of view attributes on session refresh ===

      {"M retain view attributes W last-active view is recreated after session refresh",
       [](dd_rum_config*) {},
       [](dd_rum_t* rum, MockClock& clock) {
         dd_attribute_t int_val = dd_attribute_int(0);

         // When we set global attributes {"bravo":2,"charlie":3}
         dd_attribute_set_int(&int_val, 2);
         dd_rum_add_attribute(rum, "bravo", &int_val);
         dd_attribute_set_int(&int_val, 3);
         dd_rum_add_attribute(rum, "charlie", &int_val);

         // And we start a view with {"baker":200,"charlie":300}
         dd_attribute_t start_view_obj = dd_attribute_object(3);
         dd_attribute_set_int(&int_val, 200);
         dd_attribute_object_property_set(&start_view_obj, "baker", &int_val);
         dd_attribute_set_int(&int_val, 300);
         dd_attribute_object_property_set(&start_view_obj, "charlie", &int_val);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_obj);
         dd_attribute_free(&start_view_obj);

         // And we allow 30 minutes to pass without user activity, such that on the next
         // user action we record, the initial session will be considered expired and a
         // new session will be created to replace it, with our original view being
         // recreated in that new session
         clock.Tick(std::chrono::minutes(30));

         // And we subsequently record a user interaction to trigger session refresh and
         // view transfer, making it a discrete custom attribute so it'll send an event
         // immediately, and providing {"baker":22,"dog":44} as action attributes
         dd_attribute_t add_action_obj = dd_attribute_object(2);
         dd_attribute_set_int(&int_val, 22);
         dd_attribute_object_property_set(&add_action_obj, "baker", &int_val);
         dd_attribute_set_int(&int_val, 44);
         dd_attribute_object_property_set(&add_action_obj, "dog", &int_val);
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "instant!", &add_action_obj);
         dd_attribute_free(&add_action_obj);

         // Cleanup
         dd_attribute_free(&int_val);
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

      // === Actions (continuous: start_action(), stop_action()) ===

      {"M not send action event W action remains active",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock&) {
         // When we create a RUM view and record an action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);
       },
       [](const nlohmann::json& events) {
         // Then we don't end up with any action events, because an action scope only
         // sends a single event upon being closed
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         REQUIRE(events[0]["type"] == "view");
       }},

      {"M send action event W continuous action is stopped immediately",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock&) {
         // When we create a RUM view and record an action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);

         // And we then stop that action explicitly
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and record an action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);

         // And we then stop that action explicitly after a 2-second delay
         clock.Tick(std::chrono::seconds(2));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and record an action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);

         // And we wait 15 seconds and initiate any RUM operation that will result in a
         // command being processed by the active action scope
         clock.Tick(std::chrono::seconds(15));
         dd_rum_remove_view_attribute(rum, "my-view", "nonexistent");
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and record an action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);

         // And then 4s later, we initiate any RUM operation that will result in a
         // command being processed by the active action scope
         clock.Tick(std::chrono::seconds(4));
         dd_rum_remove_view_attribute(rum, "my-view", "nonexistent");
       },
       [](const nlohmann::json& events) {
         // Then we don't end up with any action events, because at T+4s, the scope for
         // our continuous action is still active
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         REQUIRE(events[0]["type"] == "view");
       }},

      // === Actions (discrete: add_action()) ===

      {"M send immediate action event W discrete action has a type of custom",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock&) {
         // When we create a RUM view and add a custom discrete action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "custom1", NULL);
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock&) {
         // When we create a RUM view and add a discrete action whose type is not custom
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and add a discrete action whose type is not custom
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 150ms later, we initiate any RUM operation that will result in a
         // command being processed by the active action scope
         clock.Tick(std::chrono::milliseconds(150));
         dd_rum_remove_view_attribute(rum, "my-view", "nonexistent");
       },
       [](const nlohmann::json& events) {
         // Then an action event gets sent, and its duration is clamped at 100ms
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["date"] == 1700000000000);
         REQUIRE(actions[0]["action"]["loading_time"] == 100000000);
       }},

      {"M send action event W discrete action is explicitly stopped",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and add a discrete action whose type is not custom
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 50ms later, we explicitly stop the current action
         clock.Tick(std::chrono::milliseconds(50));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start a continuous custom action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "long-custom", NULL);

         // And then at T+2s, we add a discrete custom action
         clock.Tick(std::chrono::seconds(2));
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "instant-custom", NULL);

         // And then at T+4s, we stop our original continuous action
         clock.Tick(std::chrono::seconds(2));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "long-custom", NULL);
       },
       [](const nlohmann::json& events) {
         // Then we went up with two action events
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

      // === Parameters passed to dd_rum_stop_action() ===

      {"M stop current action W dd_rum_stop_action is called, regardless of type",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start an action with type 'click'
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 50ms later, we explicitly stop the current action, passing a
         // different action type of 'swipe'
         clock.Tick(std::chrono::milliseconds(50));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_SWIPE, "button1", NULL);
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

      {"M stop current action W dd_rum_stop_action is called with null name",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start an action with name "button1"
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 50ms later, we explicitly stop the current action, passing no name
         clock.Tick(std::chrono::milliseconds(50));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CLICK, NULL, NULL);
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

      {"M stop current action W dd_rum_stop_action is called with empty name",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start an action with name "button1"
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 50ms later, we explicitly stop the current action, passing no name
         clock.Tick(std::chrono::milliseconds(50));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CLICK, "", NULL);
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

      {"M rename and stop current action W dd_rum_stop_action is called with different "
       "name",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start an action with name "button1"
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 50ms later, we explicitly stop the current action, passing a
         // different name of "boton2"
         clock.Tick(std::chrono::milliseconds(50));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CLICK, "boton2", NULL);
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

      {"M stop current action W dd_rum_stop_view is called",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start an action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 50ms later, we explicitly stop the current view
         clock.Tick(std::chrono::milliseconds(50));
         dd_rum_stop_view(rum, "my-view");
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

      {"M stop current action W dd_rum_start_view is called",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start an action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 50ms later, we start a new view, effectively ending the current one
         clock.Tick(std::chrono::milliseconds(50));
         dd_rum_start_view(rum, "another-view", "Another View");
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

      {"M stop current action W dd_rum_stop_session is called",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start an action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 50ms later, we explicitly stop the session
         clock.Tick(std::chrono::milliseconds(50));
         dd_rum_stop_session(rum);
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start an action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then 7h later, we attempt to stop the action
         clock.Tick(std::chrono::hours(7));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);
       },
       [](const nlohmann::json& events) {
         // Then we get no action events: when a session expires with an action still
         // active, that action is simply dropped
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 0);
       }},

      // === Action lifetime vis-a-vis resources ===

      {"M extend discrete action lifetime W concurrent resource remains active",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and add an action with a type other than custom
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then at T+50ms, a resource begins
         clock.Tick(std::chrono::milliseconds(50));
         // TODO(RUM-12202): dd_rum_start_resource()

         // And then at T+150ms, the resource ends
         clock.Tick(std::chrono::milliseconds(100));
         // TODO(RUM-12202): dd_rum_stop_resource()
       },
       [](const nlohmann::json& events) {
         // TODO(RUM-12202): Then:
         // - Our action event is sent, its resource count is 1, and its duration is
         //   clamped at 100ms despite the fact that the scope persisted for 150ms
         // - We get a resource event, and its action.id matches the action event
         auto actions = filter_events("action", events);
         auto resources = filter_events("resource", events);
         (void)actions;
         (void)resources;
       }},

      {"M extend continuous action lifetime W concurrent resource remains active",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and start a RUM action
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_SCROLL, "scroll1", NULL);

         // And then at T+9.8s, a resource begins
         clock.Tick(std::chrono::milliseconds(9800));
         // TODO(RUM-12202): dd_rum_start_resource()

         // And then at T+14.8s, the resource ends
         clock.Tick(std::chrono::seconds(5));
         // TODO(RUM-12202): dd_rum_stop_resource()
       },
       [](const nlohmann::json& events) {
         // TODO(RUM-12202): Then:
         // - Our action event is sent, its resource count is 1, and its duration is
         //   clamped at 10s despite the fact that the scope persisted for 14.8s
         // - We get a resource event, and its action.id matches the action event
         auto actions = filter_events("action", events);
         auto resources = filter_events("resource", events);
         (void)actions;
         (void)resources;
       }},

      {"M not extend action lifetime W resource is started after action timeout",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and add an action with a type other than custom
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then at T+150ms, a resource begins
         clock.Tick(std::chrono::milliseconds(150));
         // TODO(RUM-12202): dd_rum_start_resource()

         // And then at T+200ms, the resource ends
         clock.Tick(std::chrono::milliseconds(50));
         // TODO(RUM-12202): dd_rum_stop_resource()
       },
       [](const nlohmann::json& events) {
         // TODO(RUM-12202): Then:
         // - Our action event is sent, its resource count is 0, and its duration is
         //   clamped at 100ms
         // - We get a resource event, and it has no action.id
         auto actions = filter_events("action", events);
         auto resources = filter_events("resource", events);
         (void)actions;
         (void)resources;
       }},

      {"M continually extend action lifetime W concurrent resources overlap",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and add an action with a type other than custom
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then at T+50ms, a resource begins
         clock.Tick(std::chrono::milliseconds(50));
         // TODO(RUM-12202): dd_rum_start_resource()

         // And then at T+150ms, another resource begins
         clock.Tick(std::chrono::milliseconds(100));
         // TODO(RUM-12202): dd_rum_start_resource()

         // And then at T+200ms, our first resource is stopped
         clock.Tick(std::chrono::milliseconds(50));
         // TODO(RUM-12202): dd_rum_stop_resource()
       },
       [](const nlohmann::json& events) {
         // TODO(RUM-12202): Then:
         // - Our action event is not sent because our second resource call is still
         //   pending
         // - We get a single resource event, and it has a valid action.id
         auto actions = filter_events("action", events);
         auto resources = filter_events("resource", events);
         (void)actions;
         (void)resources;
       }},

      {"M respect StopAction even W concurrent resource remains actives",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         // When we create a RUM view and add an action with a type other than custom
         dd_rum_start_view(rum, "my-view", "My View");
         dd_rum_add_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);

         // And then at T+50ms, a resource begins
         clock.Tick(std::chrono::milliseconds(50));
         // TODO(RUM-12202): dd_rum_start_resource()

         // And then at T+70ms, we explicitly stop the action
         clock.Tick(std::chrono::milliseconds(20));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CLICK, "button1", NULL);
       },
       [](const nlohmann::json& events) {
         // TODO(RUM-12202): Then:
         // - Our action event is sent, its duration is 70ms, and its resource count is
         //   zero
         // - We get no resource events
         auto actions = filter_events("action", events);
         auto resources = filter_events("resource", events);
         (void)actions;
         (void)resources;
       }},

      // === Action attributes, merging with view and global attributes ===

      {"M include action-level attributes as context W provided on start",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         dd_attribute_t int_val = dd_attribute_int(1);

         // When we create a RUM view and start an action with {"alpha":1,"bravo":2}
         dd_rum_start_view(rum, "my-view", "My View");
         dd_attribute_t start_action_obj = dd_attribute_object(2);
         dd_attribute_object_property_set(&start_action_obj, "alpha", &int_val);
         dd_attribute_set_int(&int_val, 2);
         dd_attribute_object_property_set(&start_action_obj, "bravo", &int_val);
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "foo", &start_action_obj);
         dd_attribute_free(&start_action_obj);

         // And then at T+2s, we explicitly stop the action
         clock.Tick(std::chrono::seconds(2));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "foo", NULL);

         // Cleanup
         dd_attribute_free(&int_val);
       },
       [](const nlohmann::json& events) {
         // Then the RUM event produced for our action has {"alpha":1,"bravo":2}
         auto actions = filter_events("action", events);
         REQUIRE(actions.size() == 1);
         REQUIRE(actions[0]["context"] == nlohmann::json{{"alpha", 1}, {"bravo", 2}});
       }},

      {"M merge command attributes into action-level attributes W command is "
       "StopAction",
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         dd_attribute_t int_val = dd_attribute_int(1);

         // When we create a RUM view and start an action with {"alpha":1,"bravo":2}
         dd_rum_start_view(rum, "my-view", "My View");
         dd_attribute_t start_action_obj = dd_attribute_object(2);
         dd_attribute_object_property_set(&start_action_obj, "alpha", &int_val);
         dd_attribute_set_int(&int_val, 2);
         dd_attribute_object_property_set(&start_action_obj, "bravo", &int_val);
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "foo", &start_action_obj);
         dd_attribute_free(&start_action_obj);

         // And then at T+2s, we stop the action with {"bravo":22,"charlie":33}
         clock.Tick(std::chrono::seconds(2));
         dd_attribute_t stop_action_obj = dd_attribute_object(2);
         dd_attribute_set_int(&int_val, 22);
         dd_attribute_object_property_set(&stop_action_obj, "bravo", &int_val);
         dd_attribute_set_int(&int_val, 33);
         dd_attribute_object_property_set(&stop_action_obj, "charlie", &int_val);
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "foo", &stop_action_obj);
         dd_attribute_free(&stop_action_obj);

         // Cleanup
         dd_attribute_free(&int_val);
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         dd_attribute_t int_val = dd_attribute_int(1);

         // When we create a RUM view and start an action with {"alpha":1,"bravo":2}
         dd_rum_start_view(rum, "my-view", "My View");
         dd_attribute_t start_action_obj = dd_attribute_object(2);
         dd_attribute_object_property_set(&start_action_obj, "alpha", &int_val);
         dd_attribute_set_int(&int_val, 2);
         dd_attribute_object_property_set(&start_action_obj, "bravo", &int_val);
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "foo", &start_action_obj);
         dd_attribute_free(&start_action_obj);

         // And then at T+2s, we stop the _view_ with {"bravo":22,"charlie":33}
         clock.Tick(std::chrono::seconds(2));
         dd_attribute_t stop_view_obj = dd_attribute_object(2);
         dd_attribute_set_int(&int_val, 22);
         dd_attribute_object_property_set(&stop_view_obj, "bravo", &int_val);
         dd_attribute_set_int(&int_val, 33);
         dd_attribute_object_property_set(&stop_view_obj, "charlie", &int_val);
         dd_rum_stop_view(rum, "my-view");
         dd_attribute_free(&stop_view_obj);

         // Cleanup
         dd_attribute_free(&int_val);
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
       [](dd_rum_config_t*) {
         // Given an ordinary RUM config
       },
       [](dd_rum_t* rum, MockClock& clock) {
         dd_attribute_t int_val = dd_attribute_int(1);

         // Given global RUM attributes {"able":100,"baker":200}
         dd_attribute_set_int(&int_val, 100);
         dd_rum_add_attribute(rum, "able", &int_val);
         dd_attribute_set_int(&int_val, 200);
         dd_rum_add_attribute(rum, "baker", &int_val);

         // And a view with attributes {"baker":222,"charlie":333,"dog":444}
         dd_attribute_t start_view_obj = dd_attribute_object(3);
         dd_attribute_set_int(&int_val, 222);
         dd_attribute_object_property_set(&start_view_obj, "baker", &int_val);
         dd_attribute_set_int(&int_val, 333);
         dd_attribute_object_property_set(&start_view_obj, "charlie", &int_val);
         dd_attribute_set_int(&int_val, 444);
         dd_attribute_object_property_set(&start_view_obj, "dog", &int_val);
         dd_rum_start_view_obj(rum, "my-view", "My View", &start_view_obj);
         dd_attribute_free(&start_view_obj);

         // When we start an action with {"alpha":1,"bravo":2,"dog":"good"}
         dd_attribute_t start_action_obj = dd_attribute_object(3);
         dd_attribute_set_int(&int_val, 1);
         dd_attribute_object_property_set(&start_action_obj, "alpha", &int_val);
         dd_attribute_set_int(&int_val, 2);
         dd_attribute_object_property_set(&start_action_obj, "bravo", &int_val);
         dd_attribute_t str_good = dd_attribute_string("good");
         dd_attribute_object_property_set(&start_action_obj, "dog", &str_good);
         dd_attribute_free(&str_good);
         dd_rum_start_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "foo", &start_action_obj);
         dd_attribute_free(&start_action_obj);

         // And then at T+2s, we explicitly stop the action
         clock.Tick(std::chrono::seconds(2));
         dd_rum_stop_action(rum, DD_RUM_ACTION_TYPE_CUSTOM, "foo", NULL);

         // Cleanup
         dd_attribute_free(&int_val);
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
      nlohmann::json events;
      if (!test.client.requests.empty()) {
        events = MergeJsonArrays(test.client.requests);
      }

      // And the SDK produces no diagnostic errors or warnings
      test.Diagnostics().RequireNoWarnings().RequireNoErrors();

      // And the assertions in our test case's assert callback hold true
      tt.assert_func(events);

      // Cleanup
      dd_rum_destroy(rum);
      dd_core_destroy(core);
    }
  }
}
