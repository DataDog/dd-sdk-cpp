// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/feature_types/rum.hpp"

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
    ev.source = RumSource::Unity;

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

    // RumViewEvent::Application
    ev.application.id = *UUID::Parse("a4b9f39a-e5de-45b5-bb70-a6e616bfec6c");
    ev.application.current_locale = "en-US";

    // RumViewEvent::Session
    ev.session.id = *UUID::Parse("a4b9f39a-e5de-45b5-bb70-a6e616bfec6c");
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
      "source": "unity",
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
      "application": {
        "id": "a4b9f39a-e5de-45b5-bb70-a6e616bfec6c",
        "current_locale": "en-US"
      },
      "session": {
        "id": "a4b9f39a-e5de-45b5-bb70-a6e616bfec6c",
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
