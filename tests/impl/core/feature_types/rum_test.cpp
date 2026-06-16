// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/feature_types/rum.hpp"

#include <nlohmann/json.hpp>
#include <string_view>

#include "support/catch.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("RumViewEvent", "[unit][feature_types][rum]") {
  // Given a RumViewEvent initialized with the minimum set of required properties
  const Timestamp date{std::chrono::nanoseconds(946684799999999999)};
  const UUID application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
  const UUID session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
  const RumSessionType session_type = RumSessionType::User;
  const UUID view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
  const std::string_view view_url = "my-view";
  const uint64_t view_time_spent = 0;
  const uint64_t view_action_count = 0;
  const uint64_t view_error_count = 0;
  const uint64_t view_resource_count = 0;
  const uint64_t internal_document_version = 0;
  RumViewEvent ev{
      date,
      application_id,
      session_id,
      session_type,
      view_id,
      view_url,
      view_time_spent,
      view_action_count,
      view_error_count,
      view_resource_count,
      internal_document_version
  };

  SECTION("M produce a minimal view event W only required values are set") {
    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view",
        "time_spent": 0,
        "action": {"count": 0},
        "error": {"count": 0},
        "resource": {"count": 0}
      },
      "_dd": {
        "format_version": 2,
        "document_version": 0
      },
      "type": "view"
    })"));
  }

  SECTION("M include all properties W non-required values are set") {
    // When we initialize some additional properties before serializing the event
    ev.session.is_active = false;
    ev.view.cpu_ticks_count.value = 1337;
    ev.usr.value.emplace();
    ev.usr.value->email = "foo@bar.com";
    ev.usr.value->name = "John Foo";
    ev.ddtags = "env:prod,foo:bar";
    ev._dd.session.value.emplace();
    ev._dd.session.value->plan = 1;
    ev._dd.session.value->session_precondition = RumSessionPrecondition::ExplicitStop;
    ev._dd.configuration.value.emplace(50.0f);

    // And we modify some existing values as well
    ev.view.time_spent = 130000100;
    ev.view.action.count = 1;
    ev.view.error.count = 3;
    ev._dd.document_version = 11;

    // Then our JSON event payload reflects all changes
    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user",
        "is_active": false
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view",
        "time_spent": 130000100,
        "action": {"count": 1},
        "error": {"count": 3},
        "resource": {"count": 0},
        "cpu_ticks_count": 1337
      },
      "usr": {
        "email": "foo@bar.com",
        "name": "John Foo"
      },
      "ddtags": "env:prod,foo:bar",
      "_dd": {
        "format_version": 2,
        "document_version": 11,
        "session": {
          "plan": 1,
          "session_precondition": "explicit_stop"
        },
        "configuration": {
          "session_sample_rate": 50
        }
      },
      "type": "view"
    })"));
  }

  SECTION("M generate valid event W all supported values are set") {
    // RumViewEvent
    ev.date = Timestamp{std::chrono::nanoseconds(1761829845132015612)};
    ev.service = "my-service";
    ev.version = "my-version";
    ev.build_version = "my-build-version";
    ev.build_id = "d2008244-7344-4313-a7df-b1c283c995c1";
    ev.ddtags = "service:my-service,env:test,foo:bar";
    ev.source = RumSource::RumCpp;

    // RumUserProperties
    ev.usr.value.emplace();
    ev.usr.value->id = "390cfcd41";
    ev.usr.value->name = "John Q. Public";
    ev.usr.value->email = "jqpublic@example.com";
    ev.usr.value->anonymous_id = "a52beca3-34c1-4e35-9c26-d8a2daa212e6";

    // RumAccountProperties
    ev.account.value.emplace("708876d3e663c2eb");
    ev.account.value->name = "Important Account";

    // RumConnectivityProperties
    ev.connectivity.value.emplace(RumConnectivityStatus::Connected);

    // RumViewEvent::Display
    ev.display.value.emplace();
    ev.display.value->viewport.value.emplace(1280, 720);

    // RumSyntheticsProperties
    ev.synthetics.value.emplace("test-8e81e7", "result-6049db");
    ev.synthetics.value->injected = true;

    // RumCITestProperties
    ev.ci_test.value.emplace("execution-90cfcd");

    // RumOSProperties
    ev.os.value.emplace("Windows", "98", "4");
    ev.os.value->build = "4.10.2222 A";

    // RumDeviceProperties
    ev.device.value.emplace();
    ev.device.value->type = RumDeviceType::Desktop;
    ev.device.value->name = "Packard Bell Legend 486";
    ev.device.value->model = "486DX4-100";
    ev.device.value->brand = "Packard Bell";
    ev.device.value->architecture = "x86 [Intel 80486]";
    ev.device.value->locale = "en-US";

    // RumViewEvent::Application
    ev.application.id = *UUID::Parse("a4b9f39a-e5de-45b5-bb70-a6e616bfec6c");
    ev.application.current_locale = "en-US";

    // RumViewEvent::Session
    ev.session.id = *UUID::Parse("f1f719db-ed81-4e63-9fe9-cf434c2af8e6");
    ev.session.type = RumSessionType::Synthetics;
    ev.session.has_replay = true;
    ev.session.is_active = true;
    ev.session.sampled_for_replay = true;

    // RumViewEvent::View
    ev.view.id = *UUID::Parse("18136cf5-e4a8-4e5c-9d65-7cab1703f17f");
    ev.view.referrer = "https://referer.referrer";
    ev.view.url = "https://example.com/yes";
    ev.view.name = "Yes!!!🙌";
    ev.view.loading_time = 1000000040;
    ev.view.network_settled_time = 1000000050;
    ev.view.interaction_to_next_view_time = 1000000060;
    ev.view.loading_type = RumViewLoadingType::RouteChange;
    ev.view.time_spent = 1505050505;
    ev.view.is_active = false;
    ev.view.is_slow_rendered = true;
    ev.view.action.count = 101;
    ev.view.error.count = 102;
    ev.view.crash.value.emplace(103);
    ev.view.long_task.value.emplace(104);
    ev.view.frozen_frame.value.emplace(105);
    ev.view.resource.count = 106;
    ev.view.frustration.value.emplace(107);
    ev.view.memory_average = 5120000;
    ev.view.memory_max = 7680000;
    ev.view.cpu_ticks_count = 333333;
    ev.view.cpu_ticks_per_second = 3.333;
    ev.view.refresh_rate_average = 16.667;
    ev.view.refresh_rate_min = 15.999;
    ev.view.slow_frames_rate = 0.5125;
    ev.view.freeze_rate = 0.175;
    ev.view.flutter_build_time.value.emplace(1.11, 2.22, 1.86);
    ev.view.flutter_build_time.value->metric_max = 5.5;
    ev.view.flutter_raster_time.value.emplace(1.22, 2.11, 1.77);
    ev.view.flutter_raster_time.value->metric_max = 6.6;
    ev.view.js_refresh_rate.value.emplace(1.12, 2.21, 1.81);
    ev.view.js_refresh_rate.value->metric_max = 7.7;

    // RumViewEvent::View::Performance
    ev.view.performance.value.emplace();
    ev.view.performance.value->cls.value.emplace(0.88);
    ev.view.performance.value->cls.value->timestamp = 10103000;
    ev.view.performance.value->cls.value->target_selector = "#main";
    ev.view.performance.value->cls.value->previous_rect.value.emplace(0, 0, 100, 200);
    ev.view.performance.value->cls.value->current_rect.value.emplace(-1, 50, 100, 250);
    ev.view.performance.value->fcp.value.emplace(10104000);
    ev.view.performance.value->fid.value.emplace(10105000, 15555);
    ev.view.performance.value->fid.value->target_selector = "#foo";
    ev.view.performance.value->inp.value.emplace(10106000);
    ev.view.performance.value->inp.value->timestamp = 16666;
    ev.view.performance.value->inp.value->target_selector = "#bar";
    ev.view.performance.value->lcp.value.emplace(10107000);
    ev.view.performance.value->lcp.value->target_selector = "#baz";
    ev.view.performance.value->lcp.value->resource_url = "http://127.0.0.1:5000/foo";
    ev.view.performance.value->fbc.value.emplace(10108000);

    // RumViewEvent::View::Accessibility
    ev.view.accessibility.value.emplace();
    ev.view.accessibility.value->text_size = "larger";
    ev.view.accessibility.value->screen_reader_enabled = true;
    ev.view.accessibility.value->bold_text_enabled = true;
    ev.view.accessibility.value->reduce_transparency_enabled = true;
    ev.view.accessibility.value->reduce_motion_enabled = true;
    ev.view.accessibility.value->button_shapes_enabled = true;
    ev.view.accessibility.value->invert_colors_enabled = true;
    ev.view.accessibility.value->increase_contrast_enabled = true;
    ev.view.accessibility.value->assistive_switch_enabled = true;
    ev.view.accessibility.value->assistive_touch_enabled = true;
    ev.view.accessibility.value->video_autoplay_enabled = true;
    ev.view.accessibility.value->closed_captioning_enabled = true;
    ev.view.accessibility.value->mono_audio_enabled = true;
    ev.view.accessibility.value->shake_to_undo_enabled = true;
    ev.view.accessibility.value->reduced_animations_enabled = true;
    ev.view.accessibility.value->should_differentiate_without_color = true;
    ev.view.accessibility.value->grayscale_enabled = true;
    ev.view.accessibility.value->single_app_mode_enabled = true;
    ev.view.accessibility.value->on_off_switch_labels_enabled = true;
    ev.view.accessibility.value->speak_screen_enabled = true;
    ev.view.accessibility.value->speak_selection_enabled = true;
    ev.view.accessibility.value->rtl_enabled = true;

    // RumViewEvent::Internal::Session
    ev._dd.session.value.emplace();
    ev._dd.session.value->plan = 2;
    ev._dd.session.value->session_precondition =
        RumSessionPrecondition::InactivityTimeout;

    // RumViewEvent::Internal::Configuration
    ev._dd.configuration.value.emplace(10.0f);
    ev._dd.configuration.value->session_replay_sample_rate = 5.0f;
    ev._dd.configuration.value->profiling_sample_rate = 20.0f;
    ev._dd.configuration.value->start_session_replay_recording_manually = false;

    // RumViewEvent::Internal
    ev._dd.browser_sdk_version = "3.1.2";
    ev._dd.document_version = 121;

    // Custom user attributes (RumViewEvent::context)
    ev.context.value.InitObject(8);
    ev.context.value.SetObjectProperty("foo", Attribute::Int(100));
    auto coord = Attribute::Object(2);
    coord.SetObjectProperty("x", Attribute::Double(33.3));
    coord.SetObjectProperty("y", Attribute::Double(-14.1));
    ev.context.value.SetObjectProperty("coord", coord);
    ev.context.value.SetObjectProperty("service", Attribute::String("arbitrary-value"));

    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "view",
      "date": 1761829845132,
      "service": "my-service",
      "version": "my-version",
      "build_version": "my-build-version",
      "build_id": "d2008244-7344-4313-a7df-b1c283c995c1",
      "ddtags": "service:my-service,env:test,foo:bar",
      "source": "rum-cpp",
      "usr": {
        "id": "390cfcd41",
        "name": "John Q. Public",
        "email": "jqpublic@example.com",
        "anonymous_id": "a52beca3-34c1-4e35-9c26-d8a2daa212e6"
      },
      "account": {
        "id": "708876d3e663c2eb",
        "name": "Important Account"
      },
      "connectivity": {
        "status": "connected"
      },
      "display": {
        "viewport": {
          "width": 1280,
          "height": 720
        }
      },
      "synthetics": {
        "test_id": "test-8e81e7",
        "result_id": "result-6049db",
        "injected": true
      },
      "ci_test": {
        "test_execution_id": "execution-90cfcd"
      },
      "os": {
        "name": "Windows",
        "version": "98",
        "build": "4.10.2222 A",
        "version_major": "4"
      },
      "device": {
        "type": "desktop",
        "name": "Packard Bell Legend 486",
        "model": "486DX4-100",
        "brand": "Packard Bell",
        "architecture": "x86 [Intel 80486]",
        "locale": "en-US"
      },
      "application": {
        "id": "a4b9f39a-e5de-45b5-bb70-a6e616bfec6c",
        "current_locale": "en-US"
      },
      "session": {
        "id": "f1f719db-ed81-4e63-9fe9-cf434c2af8e6",
        "type": "synthetics",
        "has_replay": true,
        "is_active": true,
        "sampled_for_replay": true
      },
      "view": {
        "id": "18136cf5-e4a8-4e5c-9d65-7cab1703f17f",
        "referrer": "https://referer.referrer",
        "url": "https://example.com/yes",
        "name": "Yes!!!🙌",
        "loading_time": 1000000040,
        "network_settled_time": 1000000050,
        "interaction_to_next_view_time": 1000000060,
        "loading_type": "route_change",
        "time_spent": 1505050505,
        "is_active": false,
        "is_slow_rendered": true,
        "action": {"count": 101},
        "error": {"count": 102},
        "crash": {"count": 103},
        "long_task": {"count": 104},
        "frozen_frame": {"count": 105},
        "resource": {"count": 106},
        "frustration": {"count": 107},
        "memory_average": 5120000,
        "memory_max": 7680000,
        "cpu_ticks_count": 333333,
        "cpu_ticks_per_second": 3.333,
        "refresh_rate_average": 16.667,
        "refresh_rate_min": 15.999,
        "slow_frames_rate": 0.5125,
        "freeze_rate": 0.175,
        "flutter_build_time": {
          "min": 1.11,
          "max": 2.22,
          "average": 1.86,
          "metric_max": 5.5
        },
        "flutter_raster_time": {
          "min": 1.22,
          "max": 2.11,
          "average": 1.77,
          "metric_max": 6.6
        },
        "js_refresh_rate": {
          "min": 1.12,
          "max": 2.21,
          "average": 1.81,
          "metric_max": 7.7
        },
        "performance": {
          "cls": {
            "score": 0.88,
            "timestamp": 10103000,
            "target_selector": "#main",
            "previous_rect": {
              "x": 0,
              "y": 0,
              "width": 100,
              "height": 200
            },
            "current_rect": {
              "x": -1,
              "y": 50,
              "width": 100,
              "height": 250
            }
          },
          "fcp": {
            "timestamp": 10104000
          },
          "fid": {
            "duration": 10105000,
            "timestamp": 15555,
            "target_selector": "#foo"
          },
          "inp": {
            "duration": 10106000,
            "timestamp": 16666,
            "target_selector": "#bar"
          },
          "lcp": {
            "timestamp": 10107000,
            "target_selector": "#baz",
            "resource_url": "http://127.0.0.1:5000/foo"
          },
          "fbc": {
            "timestamp": 10108000
          }
        },
        "accessibility": {
          "text_size": "larger",
          "screen_reader_enabled": true,
          "bold_text_enabled": true,
          "reduce_transparency_enabled": true,
          "reduce_motion_enabled": true,
          "button_shapes_enabled": true,
          "invert_colors_enabled": true,
          "increase_contrast_enabled": true,
          "assistive_switch_enabled": true,
          "assistive_touch_enabled": true,
          "video_autoplay_enabled": true,
          "closed_captioning_enabled": true,
          "mono_audio_enabled": true,
          "shake_to_undo_enabled": true,
          "reduced_animations_enabled": true,
          "should_differentiate_without_color": true,
          "grayscale_enabled": true,
          "single_app_mode_enabled": true,
          "on_off_switch_labels_enabled": true,
          "speak_screen_enabled": true,
          "speak_selection_enabled": true,
          "rtl_enabled": true
        }
      },
      "context": {
        "foo": 100,
        "coord": {"x": 33.3, "y": -14.1},
        "service": "arbitrary-value"
      },
      "_dd": {
        "format_version": 2,
        "session": {
          "plan": 2,
          "session_precondition": "inactivity_timeout"
        },
        "configuration": {
          "session_sample_rate": 10,
          "session_replay_sample_rate": 5,
          "profiling_sample_rate": 20,
          "start_session_replay_recording_manually": false
        },
        "browser_sdk_version": "3.1.2",
        "document_version": 121
      }
    })"));
  }
}

TEST_CASE("RumActionEvent", "[unit][feature_types][rum]") {
  // Given a RumActionEvent initialized with the minimum set of required properties
  const Timestamp date{std::chrono::nanoseconds(946684799999999999)};
  const UUID application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
  const UUID session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
  const RumSessionType session_type = RumSessionType::User;
  const UUID view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
  const std::string_view view_url = "my-view";
  const RumActionType action_type = RumActionType::Click;
  const UUID action_id = *UUID::Parse("4c10171e-4334-4334-4334-b0000eeeefff");
  const Duration action_duration = std::chrono::milliseconds(55);
  RumActionEvent ev{
      date,
      application_id,
      session_id,
      session_type,
      view_id,
      view_url,
      action_type,
      action_id,
      action_duration
  };

  SECTION("M produce a minimal action event W only required values are set") {
    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view"
      },
      "action": {
        "type": "click",
        "id": "4c10171e-4334-4334-4334-b0000eeeefff",
        "loading_time": 55000000
      },
      "_dd": {
        "format_version": 2
      },
      "type": "action"
    })"));
  }

  SECTION("M generate valid event W all supported values are set") {
    // RumActionEvent
    ev.date = Timestamp{std::chrono::nanoseconds(1761829845132015612)};
    ev.service = "my-service";
    ev.version = "my-version";
    ev.build_version = "my-build-version";
    ev.build_id = "d2008244-7344-4313-a7df-b1c283c995c1";
    ev.ddtags = "service:my-service,env:test,foo:bar";
    ev.source = RumSource::RumCpp;

    // RumUserProperties
    ev.usr.value.emplace();
    ev.usr.value->id = "390cfcd41";
    ev.usr.value->name = "John Q. Public";
    ev.usr.value->email = "jqpublic@example.com";
    ev.usr.value->anonymous_id = "a52beca3-34c1-4e35-9c26-d8a2daa212e6";
    ev.usr.value->extra = Attribute::Object(2);
    ev.usr.value->extra.SetObjectProperty("foo", Attribute::Int(100));
    ev.usr.value->extra.SetObjectProperty("bar", Attribute::Int(200));

    // RumAccountProperties
    ev.account.value.emplace("708876d3e663c2eb");
    ev.account.value->name = "Important Account";
    ev.account.value->extra = Attribute::Object(2);
    ev.account.value->extra.SetObjectProperty("x", Attribute::Double(867.5309));
    ev.account.value->extra.SetObjectProperty("y", Attribute::Double(-99.9));

    // RumConnectivityProperties
    ev.connectivity.value.emplace(RumConnectivityStatus::Connected);

    // RumViewEvent::Display
    ev.display.value.emplace();
    ev.display.value->viewport.value.emplace(1280, 720);

    // RumSyntheticsProperties
    ev.synthetics.value.emplace("test-8e81e7", "result-6049db");
    ev.synthetics.value->injected = true;

    // RumCITestProperties
    ev.ci_test.value.emplace("execution-90cfcd");

    // RumOSProperties
    ev.os.value.emplace("Windows", "98", "4");
    ev.os.value->build = "4.10.2222 A";

    // RumDeviceProperties
    ev.device.value.emplace();
    ev.device.value->type = RumDeviceType::Desktop;
    ev.device.value->name = "Packard Bell Legend 486";
    ev.device.value->model = "486DX4-100";
    ev.device.value->brand = "Packard Bell";
    ev.device.value->architecture = "x86 [Intel 80486]";
    ev.device.value->locale = "en-US";

    // RumActionEvent::Application
    ev.application.id = *UUID::Parse("a4b9f39a-e5de-45b5-bb70-a6e616bfec6c");
    ev.application.current_locale = "en-US";

    // RumActionEvent::Session
    ev.session.id = *UUID::Parse("f1f719db-ed81-4e63-9fe9-cf434c2af8e6");
    ev.session.type = RumSessionType::Synthetics;
    ev.session.has_replay = true;

    // RumActionEvent::View
    ev.view.id = *UUID::Parse("18136cf5-e4a8-4e5c-9d65-7cab1703f17f");
    ev.view.referrer = "https://referer.referrer";
    ev.view.url = "https://example.com/yes";
    ev.view.name = "Yes!!!🙌";

    // RumActionEvent::Internal::Session
    ev._dd.session.value.emplace();
    ev._dd.session.value->plan = 2;
    ev._dd.session.value->session_precondition =
        RumSessionPrecondition::InactivityTimeout;

    // RumActionEvent::Internal::Configuration
    ev._dd.configuration.value.emplace(10.0f);
    ev._dd.configuration.value->session_replay_sample_rate = 5.0f;
    ev._dd.configuration.value->profiling_sample_rate = 20.0f;

    // RumActionEvent::Action
    ev.action.target.value.emplace("my-target");
    ev.action.error.value.emplace(4);
    ev.action.crash.value.emplace(8);
    ev.action.long_task.value.emplace(12);
    ev.action.resource.value.emplace(16);

    // RumActionEvent::Internal
    ev._dd.browser_sdk_version = "3.1.2";

    // Custom user attributes (RumActionEvent::context)
    ev.context.value.InitObject(8);
    ev.context.value.SetObjectProperty("foo", Attribute::Int(100));
    auto coord = Attribute::Object(2);
    coord.SetObjectProperty("x", Attribute::Double(33.3));
    coord.SetObjectProperty("y", Attribute::Double(-14.1));
    ev.context.value.SetObjectProperty("coord", coord);
    ev.context.value.SetObjectProperty("service", Attribute::String("arbitrary-value"));

    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "action",
      "date": 1761829845132,
      "service": "my-service",
      "version": "my-version",
      "build_version": "my-build-version",
      "build_id": "d2008244-7344-4313-a7df-b1c283c995c1",
      "ddtags": "service:my-service,env:test,foo:bar",
      "source": "rum-cpp",
      "usr": {
        "id": "390cfcd41",
        "name": "John Q. Public",
        "email": "jqpublic@example.com",
        "anonymous_id": "a52beca3-34c1-4e35-9c26-d8a2daa212e6",
        "foo": 100,
        "bar": 200
      },
      "account": {
        "id": "708876d3e663c2eb",
        "name": "Important Account",
        "x": 867.5309,
        "y": -99.9
      },
      "connectivity": {
        "status": "connected"
      },
      "display": {
        "viewport": {
          "width": 1280,
          "height": 720
        }
      },
      "synthetics": {
        "test_id": "test-8e81e7",
        "result_id": "result-6049db",
        "injected": true
      },
      "ci_test": {
        "test_execution_id": "execution-90cfcd"
      },
      "os": {
        "name": "Windows",
        "version": "98",
        "build": "4.10.2222 A",
        "version_major": "4"
      },
      "device": {
        "type": "desktop",
        "name": "Packard Bell Legend 486",
        "model": "486DX4-100",
        "brand": "Packard Bell",
        "architecture": "x86 [Intel 80486]",
        "locale": "en-US"
      },
      "application": {
        "id": "a4b9f39a-e5de-45b5-bb70-a6e616bfec6c",
        "current_locale": "en-US"
      },
      "session": {
        "id": "f1f719db-ed81-4e63-9fe9-cf434c2af8e6",
        "type": "synthetics",
        "has_replay": true
      },
      "view": {
        "id": "18136cf5-e4a8-4e5c-9d65-7cab1703f17f",
        "referrer": "https://referer.referrer",
        "url": "https://example.com/yes",
        "name": "Yes!!!🙌"
      },
      "action": {
        "type": "click",
        "id": "4c10171e-4334-4334-4334-b0000eeeefff",
        "loading_time": 55000000,
        "target": {"name": "my-target"},
        "error": {"count": 4},
        "crash": {"count": 8},
        "long_task": {"count": 12},
        "resource": {"count": 16}
      },
      "context": {
        "foo": 100,
        "coord": {"x": 33.3, "y": -14.1},
        "service": "arbitrary-value"
      },
      "_dd": {
        "format_version": 2,
        "session": {
          "plan": 2,
          "session_precondition": "inactivity_timeout"
        },
        "configuration": {
          "session_sample_rate": 10,
          "session_replay_sample_rate": 5,
          "profiling_sample_rate": 20
        },
        "browser_sdk_version": "3.1.2"
      }
    })"));
  }
}

TEST_CASE("RumResourceEvent", "[unit][feature_types][rum]") {
  // Given a RumResourceEvent initialized with the minimum set of required properties
  const Timestamp date{std::chrono::nanoseconds(946684799999999999)};
  const UUID application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
  const UUID session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
  const RumSessionType session_type = RumSessionType::User;
  const UUID view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
  const std::string_view view_url = "my-view";
  const UUID resource_id = *UUID::Parse("8e5048ce-4444-4444-4444-bbb000eeefff");
  const RumResourceType resource_type = RumResourceType::Fetch;
  const std::string_view resource_url = "https://my-cool-website.biz/api/profile/123";
  RumResourceEvent ev{
      date,
      application_id,
      session_id,
      session_type,
      view_id,
      view_url,
      resource_id,
      resource_type,
      resource_url
  };

  SECTION("M produce a minimal resource event W only required values are set") {
    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view"
      },
      "resource": {
        "id": "8e5048ce-4444-4444-4444-bbb000eeefff",
        "type": "fetch",
        "url": "https://my-cool-website.biz/api/profile/123"
      },
      "_dd": {
        "format_version": 2
      },
      "type": "resource"
    })"));
  }

  SECTION("M generate valid event W all supported values are set") {
    // RumResourceEvent
    ev.date = Timestamp{std::chrono::nanoseconds(1761829845132015612)};
    ev.service = "my-service";
    ev.version = "my-version";
    ev.build_version = "my-build-version";
    ev.build_id = "d2008244-7344-4313-a7df-b1c283c995c1";
    ev.ddtags = "service:my-service,env:test,foo:bar";
    ev.source = RumSource::RumCpp;

    // RumUserProperties
    ev.usr.value.emplace();
    ev.usr.value->id = "390cfcd41";
    ev.usr.value->name = "John Q. Public";
    ev.usr.value->email = "jqpublic@example.com";
    ev.usr.value->anonymous_id = "a52beca3-34c1-4e35-9c26-d8a2daa212e6";

    // RumAccountProperties
    ev.account.value.emplace("708876d3e663c2eb");
    ev.account.value->name = "Important Account";

    // RumConnectivityProperties
    ev.connectivity.value.emplace(RumConnectivityStatus::Connected);

    // RumViewEvent::Display
    ev.display.value.emplace();
    ev.display.value->viewport.value.emplace(1280, 720);

    // RumSyntheticsProperties
    ev.synthetics.value.emplace("test-8e81e7", "result-6049db");
    ev.synthetics.value->injected = true;

    // RumCITestProperties
    ev.ci_test.value.emplace("execution-90cfcd");

    // RumOSProperties
    ev.os.value.emplace("Windows", "98", "4");
    ev.os.value->build = "4.10.2222 A";

    // RumDeviceProperties
    ev.device.value.emplace();
    ev.device.value->type = RumDeviceType::Desktop;
    ev.device.value->name = "Packard Bell Legend 486";
    ev.device.value->model = "486DX4-100";
    ev.device.value->brand = "Packard Bell";
    ev.device.value->architecture = "x86 [Intel 80486]";
    ev.device.value->locale = "en-US";

    // RumResourceEvent::Application
    ev.application.id = *UUID::Parse("a4b9f39a-e5de-45b5-bb70-a6e616bfec6c");
    ev.application.current_locale = "en-US";

    // RumResourceEvent::Session
    ev.session.id = *UUID::Parse("f1f719db-ed81-4e63-9fe9-cf434c2af8e6");
    ev.session.type = RumSessionType::Synthetics;
    ev.session.has_replay = true;

    // RumResourceEvent::View
    ev.view.id = *UUID::Parse("18136cf5-e4a8-4e5c-9d65-7cab1703f17f");
    ev.view.referrer = "https://referer.referrer";
    ev.view.url = "https://example.com/yes";
    ev.view.name = "Yes!!!🙌";

    // RumResourceEvent::Internal::Session
    ev._dd.session.value.emplace();
    ev._dd.session.value->plan = 2;
    ev._dd.session.value->session_precondition =
        RumSessionPrecondition::InactivityTimeout;

    // RumResourceEvent::Internal::Configuration
    ev._dd.configuration.value.emplace(10.0f);
    ev._dd.configuration.value->session_replay_sample_rate = 5.0f;
    ev._dd.configuration.value->profiling_sample_rate = 20.0f;

    // RumResourceEvent::Internal
    ev._dd.browser_sdk_version = "3.1.2";
    ev._dd.span_id = "927461028374650123";
    ev._dd.parent_span_id = "562837461029384756";
    ev._dd.trace_id = "4bf92f3577b34da6a3ce929d0e0e4736";
    ev._dd.rule_psr = 1.0f;
    ev._dd.discarded = true;

    // RumResourceEvent::Action
    ev.action.value.emplace(*UUID::Parse("4aa1315e-4cb3-4d32-90cf-a92bfd02c38c"));

    // RumResourceEvent::Resource
    ev.resource.id = *UUID::Parse("aa8cd99e-1393-4b73-ab22-f3d68feec429");
    ev.resource.type = RumResourceType::Native;
    ev.resource.method = RumResourceMethod::Patch;
    ev.resource.url = "https://www.example.com/api/user/profile";
    ev.resource.status_code = 204;
    ev.resource.duration = 179200000;
    ev.resource.size = 518;
    ev.resource.encoded_body_size = 220;
    ev.resource.decoded_body_size = 481;
    ev.resource.transfer_size = 199;
    ev.resource.render_blocking_status = RumResourceRenderBlockingStatus::NonBlocking;
    ev.resource.worker.value.emplace(0, 0);
    ev.resource.redirect.value.emplace(0, 0);
    ev.resource.dns.value.emplace(12000000, 0);
    ev.resource.connect.value.emplace(33300000, 12000000);
    ev.resource.ssl.value.emplace(41000000, 45300000);
    ev.resource.first_byte.value.emplace(77700000, 86300000);
    ev.resource.download.value.emplace(15200000, 164000000);
    ev.resource.protocol = "HTTP/1.1";
    ev.resource.delivery_type = RumResourceDeliveryType::Other;
    ev.resource.provider.value.emplace();
    ev.resource.provider.value->domain = "example.com";
    ev.resource.provider.value->name = "Example Provider";
    ev.resource.provider.value->type = RumResourceProviderType::FirstParty;

    // Custom user attributes (RumResourceEvent::context)
    ev.context.value.InitObject(8);
    ev.context.value.SetObjectProperty("foo", Attribute::Int(100));
    auto coord = Attribute::Object(2);
    coord.SetObjectProperty("x", Attribute::Double(33.3));
    coord.SetObjectProperty("y", Attribute::Double(-14.1));
    ev.context.value.SetObjectProperty("coord", coord);
    ev.context.value.SetObjectProperty("service", Attribute::String("arbitrary-value"));

    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "resource",
      "date": 1761829845132,
      "service": "my-service",
      "version": "my-version",
      "build_version": "my-build-version",
      "build_id": "d2008244-7344-4313-a7df-b1c283c995c1",
      "ddtags": "service:my-service,env:test,foo:bar",
      "source": "rum-cpp",
      "usr": {
        "id": "390cfcd41",
        "name": "John Q. Public",
        "email": "jqpublic@example.com",
        "anonymous_id": "a52beca3-34c1-4e35-9c26-d8a2daa212e6"
      },
      "account": {
        "id": "708876d3e663c2eb",
        "name": "Important Account"
      },
      "connectivity": {
        "status": "connected"
      },
      "display": {
        "viewport": {
          "width": 1280,
          "height": 720
        }
      },
      "synthetics": {
        "test_id": "test-8e81e7",
        "result_id": "result-6049db",
        "injected": true
      },
      "ci_test": {
        "test_execution_id": "execution-90cfcd"
      },
      "os": {
        "name": "Windows",
        "version": "98",
        "build": "4.10.2222 A",
        "version_major": "4"
      },
      "device": {
        "type": "desktop",
        "name": "Packard Bell Legend 486",
        "model": "486DX4-100",
        "brand": "Packard Bell",
        "architecture": "x86 [Intel 80486]",
        "locale": "en-US"
      },
      "application": {
        "id": "a4b9f39a-e5de-45b5-bb70-a6e616bfec6c",
        "current_locale": "en-US"
      },
      "session": {
        "id": "f1f719db-ed81-4e63-9fe9-cf434c2af8e6",
        "type": "synthetics",
        "has_replay": true
      },
      "view": {
        "id": "18136cf5-e4a8-4e5c-9d65-7cab1703f17f",
        "referrer": "https://referer.referrer",
        "url": "https://example.com/yes",
        "name": "Yes!!!🙌"
      },
      "action": {
        "id": "4aa1315e-4cb3-4d32-90cf-a92bfd02c38c"
      },
      "resource": {
        "id": "aa8cd99e-1393-4b73-ab22-f3d68feec429",
        "type": "native",
        "method": "PATCH",
        "url": "https://www.example.com/api/user/profile",
        "status_code": 204,
        "duration": 179200000,
        "size": 518,
        "encoded_body_size": 220,
        "decoded_body_size": 481,
        "transfer_size": 199,
        "render_blocking_status": "non-blocking",
        "worker": {"duration": 0, "start": 0},
        "redirect": {"duration": 0, "start": 0},
        "dns": {"duration": 12000000, "start": 0},
        "connect": {"duration": 33300000, "start": 12000000},
        "ssl": {"duration": 41000000, "start": 45300000},
        "first_byte": {"duration": 77700000, "start": 86300000},
        "download": {"duration": 15200000, "start": 164000000},
        "protocol": "HTTP/1.1",
        "delivery_type": "other",
        "provider": {
          "domain": "example.com",
          "name": "Example Provider",
          "type": "first party"
        }
      },
      "context": {
        "foo": 100,
        "coord": {"x": 33.3, "y": -14.1},
        "service": "arbitrary-value"
      },
      "_dd": {
        "format_version": 2,
        "session": {
          "plan": 2,
          "session_precondition": "inactivity_timeout"
        },
        "configuration": {
          "session_sample_rate": 10,
          "session_replay_sample_rate": 5,
          "profiling_sample_rate": 20
        },
        "browser_sdk_version": "3.1.2",
        "span_id": "927461028374650123",
        "parent_span_id": "562837461029384756",
        "trace_id": "4bf92f3577b34da6a3ce929d0e0e4736",
        "rule_psr": 1,
        "discarded": true
      }
    })"));
  }
}

TEST_CASE("RumErrorEvent", "[unit][feature_types][rum]") {
  // Given a RumErrorEvent initialized with the minimum set of required properties
  const Timestamp date{std::chrono::nanoseconds(946684799999999999)};
  const UUID application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
  const UUID session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
  const RumSessionType session_type = RumSessionType::User;
  const UUID view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
  const std::string_view view_url = "my-view";
  const std::string_view error_message = "oh no";
  const RumErrorSource error_source = RumErrorSource::Source;
  RumErrorEvent ev{
      date,
      application_id,
      session_id,
      session_type,
      view_id,
      view_url,
      error_message,
      error_source
  };

  SECTION("M produce a minimal error event W only required values are set") {
    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view"
      },
      "error": {
        "message": "oh no",
        "source": "source"
      },
      "_dd": {
        "format_version": 2
      },
      "type": "error"
    })"));
  }

  SECTION("M generate valid event W all supported values are set") {
    // RumErrorEvent
    ev.date = Timestamp{std::chrono::nanoseconds(1761829845132015612)};
    ev.service = "my-service";
    ev.version = "my-version";
    ev.build_version = "my-build-version";
    ev.build_id = "d2008244-7344-4313-a7df-b1c283c995c1";
    ev.ddtags = "service:my-service,env:test,foo:bar";
    ev.source = RumSource::RumCpp;

    // RumUserProperties
    ev.usr.value.emplace();
    ev.usr.value->id = "390cfcd41";
    ev.usr.value->name = "John Q. Public";
    ev.usr.value->email = "jqpublic@example.com";
    ev.usr.value->anonymous_id = "a52beca3-34c1-4e35-9c26-d8a2daa212e6";

    // RumAccountProperties
    ev.account.value.emplace("708876d3e663c2eb");
    ev.account.value->name = "Important Account";

    // RumConnectivityProperties
    ev.connectivity.value.emplace(RumConnectivityStatus::Connected);

    // RumViewEvent::Display
    ev.display.value.emplace();
    ev.display.value->viewport.value.emplace(1280, 720);

    // RumSyntheticsProperties
    ev.synthetics.value.emplace("test-8e81e7", "result-6049db");
    ev.synthetics.value->injected = true;

    // RumCITestProperties
    ev.ci_test.value.emplace("execution-90cfcd");

    // RumOSProperties
    ev.os.value.emplace("Windows", "98", "4");
    ev.os.value->build = "4.10.2222 A";

    // RumDeviceProperties
    ev.device.value.emplace();
    ev.device.value->type = RumDeviceType::Desktop;
    ev.device.value->name = "Packard Bell Legend 486";
    ev.device.value->model = "486DX4-100";
    ev.device.value->brand = "Packard Bell";
    ev.device.value->architecture = "x86 [Intel 80486]";
    ev.device.value->locale = "en-US";

    // RumErrorEvent::Application
    ev.application.id = *UUID::Parse("a4b9f39a-e5de-45b5-bb70-a6e616bfec6c");
    ev.application.current_locale = "en-US";

    // RumErrorEvent::Session
    ev.session.id = *UUID::Parse("f1f719db-ed81-4e63-9fe9-cf434c2af8e6");
    ev.session.type = RumSessionType::Synthetics;
    ev.session.has_replay = true;

    // RumErrorEvent::View
    ev.view.id = *UUID::Parse("18136cf5-e4a8-4e5c-9d65-7cab1703f17f");
    ev.view.referrer = "https://referer.referrer";
    ev.view.url = "https://example.com/yes";
    ev.view.name = "Yes!!!🙌";

    // RumErrorEvent::Internal::Session
    ev._dd.session.value.emplace();
    ev._dd.session.value->plan = 2;
    ev._dd.session.value->session_precondition =
        RumSessionPrecondition::InactivityTimeout;

    // RumErrorEvent::Internal::Configuration
    ev._dd.configuration.value.emplace(10.0f);
    ev._dd.configuration.value->session_replay_sample_rate = 5.0f;
    ev._dd.configuration.value->profiling_sample_rate = 20.0f;

    // RumErrorEvent::Internal
    ev._dd.browser_sdk_version = "3.1.2";
    ev._dd.trace_id = "1a2b3c4d5e6f7a8b";
    ev._dd.span_id = "9876543210";
    ev._dd.parent_span_id = "1122334455";
    ev._dd.rule_psr = 0.5f;

    // RumErrorEvent::Action
    ev.action.value.emplace(*UUID::Parse("4aa1315e-4cb3-4d32-90cf-a92bfd02c38c"));

    // RumErrorEvent::Error::Resource
    auto& resource = ev.error.resource.value.emplace(
        RumResourceMethod::Patch, 204, "https://www.example.com/api/user/profile"
    );
    auto& provider = resource.provider.value.emplace();
    provider.domain = "example.com";
    provider.name = "Example Provider";
    provider.type = RumResourceProviderType::FirstParty;

    // RumErrorEvent::Error::BinaryImage
    auto& binary_image_0 = ev.error.binary_images.value.emplace_back(
        "96A1CC557762401585BFD257CADF756A42", "GDI32.DLL", true
    );
    binary_image_0.load_address = "0x00000000FF00C000";
    binary_image_0.max_address = "0x00000000FF0DDFFF";
    binary_image_0.arch = "x86";
    auto& binary_image_1 = ev.error.binary_images.value.emplace_back(
        "BBB017F79B6A4BC89D4F7A89F00BB9559", "MYPROG.EXE", false
    );
    binary_image_1.load_address = "0x000000001000C000";
    binary_image_1.max_address = "0x000000001000CFFF";
    binary_image_1.arch = "x86";

    // RumErrorEvent::Error::Meta
    auto& meta = ev.error.meta.value.emplace();
    meta.code_type = "x86";
    meta.parent_process = "COMMAND.COM [pid 17]";
    meta.incident_identifier = "3F92A1B4-8C0D-4F79-AE11-9C0B12F4D1A7";
    meta.process = "MYPROG.EXE";
    meta.exception_type = "SIGSEGV";
    meta.exception_codes = "0x0000000D 0x00004B12";
    meta.path = "C:\\GAMES\\MYPROG\\MYPROG.EXE";

    // RumErrorEvent::Error::Csp
    auto& csp = ev.error.csp.value.emplace();
    csp.disposition = RumCspDisposition::Report;

    // RumErrorEvent::Error
    ev.error.id = *UUID::Parse("e1313013-4554-4554-4554-bbbbbb00eeff");
    ev.error.message = "Something has gone terribly wrong";
    ev.error.source = RumErrorSource::Custom;
    ev.error.stack = R"(Exception 0Dh: General Protection Fault
CS:EIP = 02AF:00004B12  SS:ESP = 13C2:0000F7A8
EAX=00000000  EBX=00000120  ECX=0000FFFF  EDX=00000003
ESI=0000A420  EDI=0000C8F0  EBP=0000F7C4  EFL=00010246
DS=12B0  ES=12B0  FS=0000  GS=0000

Fault at linear address 0004B12A (segment limit exceeded)

Stack dump (SS:ESP):
  F7A8: 02AF 4B12 12B0 0000 0000 F7C4 4B00 02AF
  F7B8: 0000 0003 C8F0 0000 A420 0000 F7E0 13C2
  F7C8: 0000 0010 4B80 02AF 4BA0 02AF 0003 0000

Last 8 instructions at CS:EIP:
  02AF:4B0A  MOV DX,[SI+04]
  02AF:4B0D  ADD AX,DX
  02AF:4B0F  MOV [DI],AX)";
    ev.error.is_crash = true;
    ev.error.type = "some-kind-of-error";
    ev.error.fingerprint = "sys-exc-0dh";
    ev.error.category = RumErrorCategory::Exception;
    ev.error.handling = RumErrorHandling::Handled;
    ev.error.handling_stack = "00001f16: foo\n00001f00: bar\n00001c33: baz\n";
    ev.error.source_type = RumErrorSourceType::NDK_IL2CPP;
    ev.error.was_truncated = true;
    ev.error.time_since_app_start = 3331;

    // Custom user attributes (RumErrorEvent::context)
    ev.context.value.InitObject(8);
    ev.context.value.SetObjectProperty("foo", Attribute::Int(100));
    auto coord = Attribute::Object(2);
    coord.SetObjectProperty("x", Attribute::Double(33.3));
    coord.SetObjectProperty("y", Attribute::Double(-14.1));
    ev.context.value.SetObjectProperty("coord", coord);
    ev.context.value.SetObjectProperty("service", Attribute::String("arbitrary-value"));

    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "error",
      "date": 1761829845132,
      "service": "my-service",
      "version": "my-version",
      "build_version": "my-build-version",
      "build_id": "d2008244-7344-4313-a7df-b1c283c995c1",
      "ddtags": "service:my-service,env:test,foo:bar",
      "source": "rum-cpp",
      "usr": {
        "id": "390cfcd41",
        "name": "John Q. Public",
        "email": "jqpublic@example.com",
        "anonymous_id": "a52beca3-34c1-4e35-9c26-d8a2daa212e6"
      },
      "account": {
        "id": "708876d3e663c2eb",
        "name": "Important Account"
      },
      "connectivity": {
        "status": "connected"
      },
      "display": {
        "viewport": {
          "width": 1280,
          "height": 720
        }
      },
      "synthetics": {
        "test_id": "test-8e81e7",
        "result_id": "result-6049db",
        "injected": true
      },
      "ci_test": {
        "test_execution_id": "execution-90cfcd"
      },
      "os": {
        "name": "Windows",
        "version": "98",
        "build": "4.10.2222 A",
        "version_major": "4"
      },
      "device": {
        "type": "desktop",
        "name": "Packard Bell Legend 486",
        "model": "486DX4-100",
        "brand": "Packard Bell",
        "architecture": "x86 [Intel 80486]",
        "locale": "en-US"
      },
      "application": {
        "id": "a4b9f39a-e5de-45b5-bb70-a6e616bfec6c",
        "current_locale": "en-US"
      },
      "session": {
        "id": "f1f719db-ed81-4e63-9fe9-cf434c2af8e6",
        "type": "synthetics",
        "has_replay": true
      },
      "view": {
        "id": "18136cf5-e4a8-4e5c-9d65-7cab1703f17f",
        "referrer": "https://referer.referrer",
        "url": "https://example.com/yes",
        "name": "Yes!!!🙌"
      },
      "action": {
        "id": "4aa1315e-4cb3-4d32-90cf-a92bfd02c38c"
      },
      "error": {
        "id": "e1313013-4554-4554-4554-bbbbbb00eeff",
        "message": "Something has gone terribly wrong",
        "source": "custom",
        "stack": "Exception 0Dh: General Protection Fault\nCS:EIP = 02AF:00004B12  SS:ESP = 13C2:0000F7A8\nEAX=00000000  EBX=00000120  ECX=0000FFFF  EDX=00000003\nESI=0000A420  EDI=0000C8F0  EBP=0000F7C4  EFL=00010246\nDS=12B0  ES=12B0  FS=0000  GS=0000\n\nFault at linear address 0004B12A (segment limit exceeded)\n\nStack dump (SS:ESP):\n  F7A8: 02AF 4B12 12B0 0000 0000 F7C4 4B00 02AF\n  F7B8: 0000 0003 C8F0 0000 A420 0000 F7E0 13C2\n  F7C8: 0000 0010 4B80 02AF 4BA0 02AF 0003 0000\n\nLast 8 instructions at CS:EIP:\n  02AF:4B0A  MOV DX,[SI+04]\n  02AF:4B0D  ADD AX,DX\n  02AF:4B0F  MOV [DI],AX",
        "is_crash": true,
        "fingerprint": "sys-exc-0dh",
        "type": "some-kind-of-error",
        "category": "Exception",
        "handling": "handled",
        "handling_stack": "00001f16: foo\n00001f00: bar\n00001c33: baz\n",
        "source_type": "ndk+il2cpp",
        "resource": {
          "method": "PATCH",
          "status_code": 204,
          "url": "https://www.example.com/api/user/profile",
          "provider": {
            "domain": "example.com",
            "name": "Example Provider",
            "type": "first party"
          }
        },
        "binary_images": [
          {
            "uuid": "96A1CC557762401585BFD257CADF756A42",
            "name": "GDI32.DLL",
            "is_system": true,
            "load_address": "0x00000000FF00C000",
            "max_address": "0x00000000FF0DDFFF",
            "arch": "x86"
          },
          {
            "uuid": "BBB017F79B6A4BC89D4F7A89F00BB9559",
            "name": "MYPROG.EXE",
            "is_system": false,
            "load_address": "0x000000001000C000",
            "max_address": "0x000000001000CFFF",
            "arch": "x86"
          }
        ],
        "was_truncated": true,
        "meta": {
          "code_type": "x86",
          "parent_process": "COMMAND.COM [pid 17]",
          "incident_identifier": "3F92A1B4-8C0D-4F79-AE11-9C0B12F4D1A7",
          "process": "MYPROG.EXE",
          "exception_type": "SIGSEGV",
          "exception_codes": "0x0000000D 0x00004B12",
          "path": "C:\\GAMES\\MYPROG\\MYPROG.EXE"
        },
        "csp": {
          "disposition": "report"
        },
        "time_since_app_start": 3331
      },
      "context": {
        "foo": 100,
        "coord": {"x": 33.3, "y": -14.1},
        "service": "arbitrary-value"
      },
      "_dd": {
        "format_version": 2,
        "session": {
          "plan": 2,
          "session_precondition": "inactivity_timeout"
        },
        "configuration": {
          "session_sample_rate": 10,
          "session_replay_sample_rate": 5,
          "profiling_sample_rate": 20
        },
        "browser_sdk_version": "3.1.2",
        "span_id": "9876543210",
        "parent_span_id": "1122334455",
        "trace_id": "1a2b3c4d5e6f7a8b",
        "rule_psr": 0.5
      }
    })"));
  }
}

TEST_CASE("RumVitalEvent", "[unit][feature_types][rum]") {
  // Given a RumVitalEvent initialized with the minimum set of required properties
  const Timestamp date{std::chrono::nanoseconds(946684799999999999)};
  const UUID application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
  const UUID session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
  const RumSessionType session_type = RumSessionType::User;
  const UUID view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
  const std::string_view view_url = "my-view";
  const UUID vital_id = *UUID::Parse("11111111-1111-4111-b111-111111111111");
  const RumVitalType vital_type = RumVitalType::OperationStep;
  const std::string_view vital_name = "checkout";
  const RumVitalStepType step_type = RumVitalStepType::Start;
  RumVitalEvent ev{
      date,
      application_id,
      session_id,
      session_type,
      view_id,
      view_url,
      vital_id,
      vital_type,
      vital_name,
      step_type
  };

  SECTION("M produce a minimal vital event W only required values are set") {
    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view"
      },
      "_dd": {
        "format_version": 2
      },
      "type": "vital",
      "vital": {
        "id": "11111111-1111-4111-b111-111111111111",
        "name": "checkout",
        "type": "operation_step",
        "step_type": "start"
      }
    })"));
  }

  SECTION("M include view name W view name is set") {
    ev.view.name.value = "Checkout Screen";
    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view",
        "name": "Checkout Screen"
      },
      "_dd": {
        "format_version": 2
      },
      "type": "vital",
      "vital": {
        "id": "11111111-1111-4111-b111-111111111111",
        "name": "checkout",
        "type": "operation_step",
        "step_type": "start"
      }
    })"));
  }

  SECTION("M include operation_key W operation_key is set") {
    ev.vital.operation_key.value = "op-abc-123";
    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view"
      },
      "_dd": {
        "format_version": 2
      },
      "type": "vital",
      "vital": {
        "id": "11111111-1111-4111-b111-111111111111",
        "name": "checkout",
        "type": "operation_step",
        "step_type": "start",
        "operation_key": "op-abc-123"
      }
    })"));
  }

  SECTION("M include failure_reason W stop event with failure") {
    RumVitalEvent end_ev{
        date,
        application_id,
        session_id,
        session_type,
        view_id,
        view_url,
        vital_id,
        vital_type,
        vital_name,
        RumVitalStepType::End
    };
    end_ev.vital.failure_reason.value = RumVitalFailureReason::Error;
    RequireJsonObject(end_ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view"
      },
      "_dd": {
        "format_version": 2
      },
      "type": "vital",
      "vital": {
        "id": "11111111-1111-4111-b111-111111111111",
        "name": "checkout",
        "type": "operation_step",
        "step_type": "end",
        "failure_reason": "error"
      }
    })"));
  }

  SECTION("M include abandoned failure_reason W operation abandoned") {
    RumVitalEvent end_ev{
        date,
        application_id,
        session_id,
        session_type,
        view_id,
        view_url,
        vital_id,
        vital_type,
        vital_name,
        RumVitalStepType::End
    };
    end_ev.vital.failure_reason.value = RumVitalFailureReason::Abandoned;
    end_ev.vital.operation_key.value = "user-42";
    RequireJsonObject(end_ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view"
      },
      "_dd": {
        "format_version": 2
      },
      "type": "vital",
      "vital": {
        "id": "11111111-1111-4111-b111-111111111111",
        "name": "checkout",
        "type": "operation_step",
        "step_type": "end",
        "operation_key": "user-42",
        "failure_reason": "abandoned"
      }
    })"));
  }

  SECTION("M include all optional properties W fully-populated event") {
    ev.ddtags = "service:my-service,env:my-env,sdk_version:0.0.0";
    ev.view.name.value = "Checkout Screen";
    ev.vital.operation_key.value = "my-op-key";
    ev.source.value = RumSource::ReactNative;
    ev.context.value = Attribute::Object();
    ev.context.value.SetObjectProperty("custom.key", Attribute::Int(42));
    ev.os.value.emplace("macOS", "14.5.0", "14");
    ev.os.value->build = "23F79";
    auto& device = ev.device.value.emplace();
    device.type = RumDeviceType::Desktop;
    device.name = "test-device";
    RequireJsonObject(ev, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 946684799999,
      "ddtags": "service:my-service,env:my-env,sdk_version:0.0.0",
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "source": "react-native",
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view",
        "name": "Checkout Screen"
      },
      "os": {
        "name": "macOS",
        "version": "14.5.0",
        "build": "23F79",
        "version_major": "14"
      },
      "device": {
        "type": "desktop",
        "name": "test-device"
      },
      "_dd": {
        "format_version": 2
      },
      "context": {
        "custom.key": 42
      },
      "type": "vital",
      "vital": {
        "id": "11111111-1111-4111-b111-111111111111",
        "name": "checkout",
        "type": "operation_step",
        "step_type": "start",
        "operation_key": "my-op-key"
      }
    })"));
  }
}
