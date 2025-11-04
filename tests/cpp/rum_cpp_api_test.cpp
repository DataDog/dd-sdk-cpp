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
      rum->AddAttribute("foo", Attribute::Int(100));
      rum->RemoveAttribute("foo");

      rum->StopSession();

      Attribute attributes = Attribute::Object(1);
      attributes.SetObjectProperty("bar", Attribute::Int(100));
      rum->StartView("foo", "My View", attributes);
      rum->AddViewAttribute("foo", "something", Attribute::Int(100));
      rum->RemoveViewAttribute("foo", "something");
      rum->StopView("foo", attributes);

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
    rum->AddAttribute("attr1", Attribute::Int(100));
    rum->RemoveAttribute("attr1");
    rum->StartView("bar", "Bar");
    rum->AddViewAttribute("bar", "attr2", Attribute::Int(100));
    rum->RemoveViewAttribute("bar", "attr2");
    rum->StopView("bar");
    rum->StopSession();
    rum->StartView("foo", "Foo");
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
    std::function<void(const nlohmann::json&)> assert_func;
  };
  std::vector<TestParams> tests = {

      // === Basic event validation ===

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
         rum->AddViewAttribute("my-view", "foo", Attribute::Int(100));

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
         rum->AddViewAttribute("my-view", "foo", Attribute::Int(100));

         // And we then set {"foo":200} immediately thereafter
         rum->AddViewAttribute("my-view", "foo", Attribute::Int(200));

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
         rum->AddViewAttribute("my-view", "foo", Attribute::Int(100));
         rum->AddViewAttribute("my-view", "bar", Attribute::Int(200));

         // And we then delete "foo" immediately thereafter
         rum->RemoveViewAttribute("my-view", "foo");

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
         rum->RemoveViewAttribute("my-view", "foo");

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

      {"M do nothing W RemoveViewAttribute called for nonexistent view",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we attempt to remove a view attribute from a view that doesn't exist
         rum->RemoveViewAttribute("nonexistent-view", "foo");
       },
       [](const nlohmann::json& events) {
         // Then nothing happens
         REQUIRE(events.is_null());
       }},

      {"M do nothing W AddViewAttribute called for nonexistent view",
       [](RumConfig&) {},
       [](std::shared_ptr<Rum>& rum, MockClock&) {
         // When we attempt to add a view attribute to a view that doesn't exist
         rum->AddViewAttribute("nonexistent-view", "foo", Attribute::Int(100));
       },
       [](const nlohmann::json& events) {
         // Then nothing happens
         REQUIRE(events.is_null());
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
         rum->RemoveViewAttribute("my-view", "baker");
         rum->AddViewAttribute("my-view", "charlie", Attribute::String("modified"));
         rum->AddViewAttribute("my-view", "dog", Attribute::Int(444));

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
         rum->RemoveViewAttribute("my-view", "charlie");

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
         // TODO(RUM-12202): Call rum->StartResource()

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
         rum->RemoveViewAttribute("my-view", "able");
         rum->AddViewAttribute("my-view", "dog", Attribute::Int(400));

         // And 1 second later, we stop the resource, allowing our original view scope
         // to close
         clock.Tick(std::chrono::seconds(1));
         // TODO(RUM-12202): Call rum->StopResource()

         // And finally, 5 seconds after that, we close the new view
         clock.Tick(std::chrono::seconds(5));
         rum->StopView("my-view");
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
         // view transfer
         // TODO(RUM-11369): Call rum->AddAction()
       },
       [](const nlohmann::json& events) {
         // Then we get the following sequence of RUM events:
         // - At T+0: a `view` with:
         //   - _dd.document_version: 0
         //   - is_active: true
         //   - view.name: "My View"
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         // - At T+1800: a `view` with:
         //   - _dd.document_version: 0
         //   - is_active: true
         //   - view.name: "My View"
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         // - At T+1800.1: an `action` with:
         //   - context: {"alpha":1,"bravo":2,"able":100,"baker":200,"charlie":300}
         REQUIRE(events.is_array());
         REQUIRE(events.size() == 1);
         // TODO(RUM-11369): Implement the assertions described above
       }},

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
      nlohmann::json events;
      if (!test.client.requests.empty()) {
        events = MergeJsonArrays(test.client.requests);
      }

      // And the assertions in our test case's assert callback hold true
      tt.assert_func(events);
    }
  }
}
