// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/crash_processing/crash_handling.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/feature_types/crash_reporting.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/util/json.hpp"
#include "datadog/impl/rum/scope.hpp"

#include "mock/clock.hpp"
#include "support/catch.hpp"
#include "support/diagnostics.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

/**
 * Returns a fully-populated RumViewEvent describing the last view that was active in a
 * process that crashed.
 */
static RumViewEvent make_last_view_event() {
  const Timestamp date{std::chrono::milliseconds(1699999980000)};
  const UUID application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
  const UUID session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
  const RumSessionType session_type = RumSessionType::User;
  const UUID view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
  const std::string_view view_url = "my-view";
  const uint64_t view_time_spent_ns = 5000000000;
  const uint64_t view_action_count = 2;
  const uint64_t view_error_count = 3;
  const uint64_t view_resource_count = 1;
  const uint64_t internal_document_version = 12;
  RumViewEvent ev{
      date,
      application_id,
      session_id,
      session_type,
      view_id,
      view_url,
      view_time_spent_ns,
      view_action_count,
      view_error_count,
      view_resource_count,
      internal_document_version
  };
  ev.view.is_active = true;
  ev.view.name = "My View";
  return ev;
}

/**
 * Returns a fully-populated mock CrashReport value describing a valid crash that our
 * test should be able to process. context.last_view_event_json will be populated with
 * the JSON-encoded representation of the given RumViewEvent, or left empty if no
 * RumViewEvent is provided.
 *
 * Individual test cases can mutate the resulting value as necessary to test different
 * scenarios.
 */
static CrashReport make_crash_report(
    std::optional<RumViewEvent> last_view_event = std::nullopt
) {
  // Prepare extra attributes for use in CrashContext
  Attribute user_extra = Attribute::Object(1);
  user_extra.SetObjectProperty("middle_initial", Attribute::String("Q"));

  Attribute account_extra = Attribute::Object(2);
  Attribute account_extra_ids = Attribute::Array(2);
  account_extra_ids.ArrayPush(Attribute::Int(8675309));
  account_extra_ids.ArrayPush(Attribute::Int(2813308004));
  account_extra.SetObjectProperty("service_tier", Attribute::String("good"));
  account_extra.SetObjectProperty("ids", account_extra_ids);

  Attribute global_rum_attributes = Attribute::Object(3);
  global_rum_attributes.SetObjectProperty("flags", Attribute::UInt(0xf0e1f));
  global_rum_attributes.SetObjectProperty("yes", Attribute::Bool(true));
  global_rum_attributes.SetObjectProperty("nah", Attribute::Null());

  // If provided with a RumViewEvent describing the active view at the time of the
  // crash, encode it to JSON for use in CrashContext
  std::string last_view_event_json;
  if (last_view_event.has_value()) {
    std::vector<uint8_t> buf;
    EncodeJson(buf, *last_view_event);
    last_view_event_json.append(buf.begin(), buf.end());
  }

  // Return a kitchen-sink CrashReport
  return CrashReport{
      0xfd,           // fault_code
      0x4000400f,     // fault_address
      0,              // fault_flags
      1234,           // pid
      111,            // tid
      1699999990000,  // timestamp (ms)
      // context:
      CrashContext{
          "my-service",              // service
          "my-env",                  // env
          "1.0.0",                   // application_version
          "rum-cpp",                 // source
          "2.0.2",                   // sdk_version
          TrackingConsent::Granted,  // tracking_consent
          "CoolOS",                  // os_name
          "3.3.3",                   // os_version
          "bbbbc001",                // os_build
          "3",                       // os_version_major
          "desktop",                 // device_type
          "Elektronika BK-84M",      // device_name
          "БК-84М Спектр",           // device_model
          "NPO Elektronika",         // device_brand
          "PDP-11",                  // device_architecture
          "tk-TM",                   // device_locale
          "Asia/Ashgabat",           // device_time_zone
          "u-13043904",              // user_id
          "Important User",          // user_name
          "person@example.com",      // user_email
          user_extra,                // user_extra
          "fujda9i8z83457",          // account_id
          "Important Account",       // account_name
          account_extra,             // account_extra
          // rum_sesion_state:
          {
              *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef"),  // application_id
              *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef"),  // session_id
              true,                                                  // is_sampled
              true,                                                  // is_active
              false,  // is_initial_session
              true,   // has_tracked_any_view,
              false   // did_start_with_replay
          },
          last_view_event_json,
          global_rum_attributes
      },
      // modules:
      {{"my-cool-program", "build-id-1", "x64", false, 0x10000000, 0x1000a000},
       {"some-system-library", "build-id-2", "x64", true, 0x1000a000, 0x1000b000},
       {"some-other-library", "build-id-3", "x64", false, 0x1000c000, 0x1000d000}},
      // stack:
      {{0x1000c100, 2, 0x100},
       {0x1000c50f, 2, 0x50f},
       {0x10004000, 0, 0x4000},
       {0x10001000, 0, 0x1000},
       {0x1000bbbb, -1, 0},
       {0x1000aaaa, 1, 0xaaa}}
  };
}

TEST_CASE("ContextThread_HandleCrashReport", "[unit][rum]") {
  // Given a diagnostic logger that will capture all messages
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  // And a mock system clock
  MockClock clock;
  clock.FreezeAtMilliseconds(1700000000000);

  // And a mock EventWriter function that will capture any RUM events produced by our
  // crash-processing routine, allowing up to 1 view event and 1 error event to be
  // captured (failing the test if any other value is produced, or if more than 1 view
  // or error is produced)
  nlohmann::json new_view_event;
  nlohmann::json new_error_event;
  EventWriter event_writer =
      [&](Block event_data, Block event_metadata, bool bypass_tracking_consent) {
        // When we process crash reports, we bypass the normal tracking consent
        // handling and instead generate events if consent was granted at the time of
        // the crash: if RUM generates any events in response to crashes, it must do
        // so with bypass_tracking_consent enabled
        REQUIRE(bypass_tracking_consent);

        // RUM events are produced without metadata
        REQUIRE(event_metadata.size() == 0);

        // RUM events are valid JSON objects
        auto obj = nlohmann::json::parse(event_data);
        REQUIRE(obj.is_object());

        // RUM's crash-processing routine may only produce events with type 'view' or
        // 'error', and no more than one of each
        REQUIRE(obj.contains("type"));
        auto type = obj["type"].get_ref<const std::string&>();
        if (type == "view") {
          REQUIRE(new_view_event.is_null());
          new_view_event = obj;
        } else {
          REQUIRE(type == "error");
          REQUIRE(new_error_event.is_null());
          new_error_event = obj;
        }
        return true;
      };

  // And a set of RumScopeDependencies
  auto make_deps = [&](float session_sample_rate = 100.0f) {
    // Use a different application_id to simulate the case where the application has
    // been reconfigured between launches, ensuring that the events generated for the
    // crash carry the originally-configured ID as contained in CrashContext
    RumConfig rum_config("99999999-4994-4994-4994-999999999999");
    rum_config.SetSessionSampleRate(session_sample_rate);
    RumScopeDependencies deps(rum_config, clock);
    deps.diagnostic_logger = logger;
    return deps;
  };

  // Given various CrashReport values, our HandleCrashReport routine produces the
  // expected set of RUM events

  SECTION("M disregard crash W no CrashContext is available") {
    // Given a crash report with no accompanying CrashContext
    auto crash = make_crash_report();
    crash.context.reset();

    // When we process that crash
    auto deps = make_deps();
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // Then no events are produced
    REQUIRE(new_view_event.is_null());
    REQUIRE(new_error_event.is_null());

    // And a warning is logged to indicate that a crash had no context: this is not
    // necessarily indicative of a bug or usage error, but it's worth signalling since
    // it's a case where the SDK is unable to convey data to intake due to external
    // conditions (e.g. timing of the crash relative to SDK startup)
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 1);
    REQUIRE_THAT(
        diagnostics.warning[0],
        Catch::Matchers::ContainsSubstring(
            "Ignoring prior-process crash report due to missing context"
        )
    );
  }

  SECTION("M disregard crash W consent was not Granted at time of crash") {
    // Given an otherwise valid crash report where the CrashContext indicates that
    // tracking consent was either pending or explicitly revoked at the time of the
    // crash
    auto consent = GENERATE(TrackingConsent::Pending, TrackingConsent::NotGranted);
    auto crash = make_crash_report();
    crash.context->tracking_consent = consent;

    // When we process that crash
    auto deps = make_deps();
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // Then no events are produced
    REQUIRE(new_view_event.is_null());
    REQUIRE(new_error_event.is_null());

    // And a status message is logged as we happily ignore the report
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
    REQUIRE(diagnostics.status.size() == 1);
    REQUIRE_THAT(
        diagnostics.status[0],
        Catch::Matchers::ContainsSubstring(
            "Ignoring prior-process crash report due to lack of tracking consent at "
            "time of crash"
        )
    );
  }

  SECTION("M disregard crash W last session was explicitly stopped prior to crash") {
    // Given an otherwise valid crash report where the RumSessionState indicates that
    // the last active session was no longer active, indicating that at the time of the
    // crash, a StopSession call had explicitly stopped tracking
    auto crash = make_crash_report();
    crash.context->rum_session_state.is_active = false;

    // When we process that crash
    auto deps = make_deps();
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // Then no events are produced
    REQUIRE(new_view_event.is_null());
    REQUIRE(new_error_event.is_null());

    // And a status message is logged as we happily ignore the report
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
    REQUIRE(diagnostics.status.size() == 1);
    REQUIRE_THAT(
        diagnostics.status[0],
        Catch::Matchers::ContainsSubstring(
            "Ignoring prior-process crash report: crash occurred after StopSession had "
            "explicitly stopped RUM tracking"
        )
    );
  }

  SECTION("M disregard crash W active session was excluded from sampling") {
    // Given a crash report where RumSessionState indicates that a session was active at
    // the time of the crash, but was excluded from sampling
    auto crash = make_crash_report();
    crash.context->rum_session_state.is_sampled = false;

    // When we process that crash
    auto deps = make_deps();
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // Then no events are produced
    REQUIRE(new_view_event.is_null());
    REQUIRE(new_error_event.is_null());

    // And a status message is logged as we happily ignore the report
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
    REQUIRE(diagnostics.status.size() == 1);
    REQUIRE_THAT(
        diagnostics.status[0],
        Catch::Matchers::ContainsSubstring(
            "Ignoring prior-process crash report: crash occurred during a session that "
            "was not sampled"
        )
    );
  }

  SECTION(
      "M produce ApplicationLaunch view (with synthesized session) and new error event "
      "W no session was active and sampling decision is affirmative"
  ) {
    // Given a crash in a process where no session has ever existed
    auto crash = make_crash_report();
    crash.context->rum_session_state.session_id = UUID::Zero;

    // When we process that crash
    auto deps = make_deps();
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // Then we produce a RUM View event that describes a synthetic 'ApplicationLaunch'
    // view created to contain the Error, where:
    // - date reflects the timestamp of the crash
    // - os, device, usr, and account reflect the data from CrashContext
    // - session.id reflects a newly-generated session
    // - view.url and view.name are magic values for ApplicationLaunch
    // - view.is_active is false, view.time_spent is 1
    // - error and crash count are 1
    REQUIRE(new_view_event.is_object());
    RequireEventMatch(new_view_event, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "view",
      "date": 1699999990000,
      "service": "my-service",
      "version": "1.0.0",
      "os": {
        "name": "CoolOS",
        "version": "3.3.3",
        "build": "bbbbc001",
        "version_major": "3"
      },
      "device": {
        "type": "desktop",
        "name": "Elektronika BK-84M",
        "model": "БК-84М Спектр",
        "brand": "NPO Elektronika",
        "architecture": "PDP-11",
        "locale": "tk-TM",
        "time_zone": "Asia/Ashgabat"
      },
      "usr": {
        "id": "u-13043904",
        "name": "Important User",
        "email": "person@example.com",
        "middle_initial": "Q"
      },
      "account": {
        "id": "fujda9i8z83457",
        "name": "Important Account",
        "service_tier": "good",
        "ids": [8675309, 2813308004]
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
        "url": "com/datadog/application-launch/view",
        "name": "ApplicationLaunch",
        "is_active": false,
        "time_spent": 1,
        "action": {"count": 0},
        "error": {"count": 1},
        "crash": {"count": 1},
        "resource": {"count": 0}
      },
      "context": {
        "flags": 986655,
        "yes": true,
        "nah": null
      },
      "_dd": {
        "format_version": 2,
        "document_version": 1
      }
    })"));

    // And we also produce a RUM Error linked to that view
    REQUIRE(new_error_event.is_object());
    REQUIRE(new_error_event["session"]["id"] == new_view_event["session"]["id"]);
    REQUIRE(new_error_event["view"]["id"] == new_view_event["view"]["id"]);
    RequireEventMatch(new_error_event, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "error",
      "date": 1699999990000,
      "service": "my-service",
      "version": "1.0.0",
      "os": {
        "name": "CoolOS",
        "version": "3.3.3",
        "build": "bbbbc001",
        "version_major": "3"
      },
      "device": {
        "type": "desktop",
        "name": "Elektronika BK-84M",
        "model": "БК-84М Спектр",
        "brand": "NPO Elektronika",
        "architecture": "PDP-11",
        "locale": "tk-TM",
        "time_zone": "Asia/Ashgabat"
      },
      "usr": {
        "id": "u-13043904",
        "name": "Important User",
        "email": "person@example.com",
        "middle_initial": "Q"
      },
      "account": {
        "id": "fujda9i8z83457",
        "name": "Important Account",
        "service_tier": "good",
        "ids": [8675309, 2813308004]
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
        "url": "com/datadog/application-launch/view",
        "name": "ApplicationLaunch"
      },
      "error": {
        "id": "${__NONZERO_UUID__}",
        "is_crash": true,
        "message": "${__ERROR_MESSAGE_APPLICATION_CRASH__}",
        "source": "source",
        "source_type": "${__ERROR_SOURCE_TYPE_PLATFORM_NAME__}",
        "stack": "0   some-other-library\t0x000000001000c100 0x1000c000 + 256\n1   some-other-library\t0x000000001000c50f 0x1000c000 + 1295\n2   my-cool-program\t0x0000000010004000 0x10000000 + 16384\n3   my-cool-program\t0x0000000010001000 0x10000000 + 4096\n4   ???\t0x000000001000bbbb 0x0 + 0\n5   some-system-library\t0x000000001000aaaa 0x1000a000 + 2730\n",
        "binary_images": [
          {
            "uuid": "build-id-1",
            "name": "my-cool-program",
            "is_system": false,
            "load_address": "10000000",
            "max_address": "1000a000",
            "arch": "${__CPU_ARCH__}"
          },
          {
            "uuid": "build-id-2",
            "name": "some-system-library",
            "is_system": true,
            "load_address": "1000a000",
            "max_address": "1000b000",
            "arch": "${__CPU_ARCH__}"
          },
          {
            "uuid": "build-id-3",
            "name": "some-other-library",
            "is_system": false,
            "load_address": "1000c000",
            "max_address": "1000d000",
            "arch": "${__CPU_ARCH__}"
          }
        ]
      },
      "context": {
        "flags": 986655,
        "yes": true,
        "nah": null
      },
      "_dd": {
        "format_version": 2
      }
    })"));

    // And no warnings or errors are logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
    REQUIRE_THAT(
        diagnostics.status.back(),
        Catch::Matchers::ContainsSubstring(
            "Handled crash report: created new session, created ApplicationLaunch "
            "view, and recorded RUM Error"
        )
    );
  }

  SECTION(
      "M disregard crash W no session was active and sampling decision is negative"
  ) {
    // Given a crash in a process where no session has ever existed
    auto crash = make_crash_report();
    crash.context->rum_session_state.session_id = UUID::Zero;

    // When we process that crash in an SDK instance that has a configured RUM session
    // sample rate of 0%
    auto deps = make_deps(0.0f);
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // Then no events are generated
    REQUIRE(new_view_event.is_null());
    REQUIRE(new_error_event.is_null());

    // And no warnings or errors are logged; just a status message indicating that we
    // dropped the crash as expected in order to respect the sampling rate
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
    REQUIRE_THAT(
        diagnostics.status.back(),
        Catch::Matchers::ContainsSubstring(
            "Ignoring prior-process crash report: newly-created session was excluded "
            "from sampling"
        )
    );
  }

  SECTION(
      "M produce ApplicationLaunch view and new error event W no view was active and "
      "application was in initial session with no views created"
  ) {
    // Given a crash in a process where no views have yet been created, and we're still
    // in the initial session
    auto crash = make_crash_report();
    crash.context->rum_session_state.is_initial_session = true;
    crash.context->rum_session_state.has_tracked_any_view = false;

    // When we process that crash
    auto deps = make_deps();
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // Then we produce a RUM View event that describes a synthetic 'ApplicationLaunch'
    // view created to contain the Error, where all details are the same as the
    // pre-initial-session case above, except session.id refers to the already-existing
    // session
    REQUIRE(new_view_event.is_object());
    RequireEventMatch(new_view_event, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "view",
      "date": 1699999990000,
      "service": "my-service",
      "version": "1.0.0",
      "os": {
        "name": "CoolOS",
        "version": "3.3.3",
        "build": "bbbbc001",
        "version_major": "3"
      },
      "device": {
        "type": "desktop",
        "name": "Elektronika BK-84M",
        "model": "БК-84М Спектр",
        "brand": "NPO Elektronika",
        "architecture": "PDP-11",
        "locale": "tk-TM",
        "time_zone": "Asia/Ashgabat"
      },
      "usr": {
        "id": "u-13043904",
        "name": "Important User",
        "email": "person@example.com",
        "middle_initial": "Q"
      },
      "account": {
        "id": "fujda9i8z83457",
        "name": "Important Account",
        "service_tier": "good",
        "ids": [8675309, 2813308004]
      },
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "${__NONZERO_UUID__}",
        "url": "com/datadog/application-launch/view",
        "name": "ApplicationLaunch",
        "is_active": false,
        "time_spent": 1,
        "action": {"count": 0},
        "error": {"count": 1},
        "crash": {"count": 1},
        "resource": {"count": 0}
      },
      "context": {
        "flags": 986655,
        "yes": true,
        "nah": null
      },
      "_dd": {
        "format_version": 2,
        "document_version": 1
      }
    })"));

    // And we also produce a RUM Error linked to that view, same as in the
    // pre-initial-session case but with the existing session.id
    REQUIRE(new_error_event.is_object());
    REQUIRE(new_error_event["view"]["id"] == new_view_event["view"]["id"]);
    RequireEventMatch(new_error_event, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "error",
      "date": 1699999990000,
      "service": "my-service",
      "version": "1.0.0",
      "os": {
        "name": "CoolOS",
        "version": "3.3.3",
        "build": "bbbbc001",
        "version_major": "3"
      },
      "device": {
        "type": "desktop",
        "name": "Elektronika BK-84M",
        "model": "БК-84М Спектр",
        "brand": "NPO Elektronika",
        "architecture": "PDP-11",
        "locale": "tk-TM",
        "time_zone": "Asia/Ashgabat"
      },
      "usr": {
        "id": "u-13043904",
        "name": "Important User",
        "email": "person@example.com",
        "middle_initial": "Q"
      },
      "account": {
        "id": "fujda9i8z83457",
        "name": "Important Account",
        "service_tier": "good",
        "ids": [8675309, 2813308004]
      },
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "user"
      },
      "view": {
        "id": "${__NONZERO_UUID__}",
        "url": "com/datadog/application-launch/view",
        "name": "ApplicationLaunch"
      },
      "error": {
        "id": "${__NONZERO_UUID__}",
        "is_crash": true,
        "message": "${__ERROR_MESSAGE_APPLICATION_CRASH__}",
        "source": "source",
        "source_type": "${__ERROR_SOURCE_TYPE_PLATFORM_NAME__}",
        "stack": "0   some-other-library\t0x000000001000c100 0x1000c000 + 256\n1   some-other-library\t0x000000001000c50f 0x1000c000 + 1295\n2   my-cool-program\t0x0000000010004000 0x10000000 + 16384\n3   my-cool-program\t0x0000000010001000 0x10000000 + 4096\n4   ???\t0x000000001000bbbb 0x0 + 0\n5   some-system-library\t0x000000001000aaaa 0x1000a000 + 2730\n",
        "binary_images": [
          {
            "uuid": "build-id-1",
            "name": "my-cool-program",
            "is_system": false,
            "load_address": "10000000",
            "max_address": "1000a000",
            "arch": "${__CPU_ARCH__}"
          },
          {
            "uuid": "build-id-2",
            "name": "some-system-library",
            "is_system": true,
            "load_address": "1000a000",
            "max_address": "1000b000",
            "arch": "${__CPU_ARCH__}"
          },
          {
            "uuid": "build-id-3",
            "name": "some-other-library",
            "is_system": false,
            "load_address": "1000c000",
            "max_address": "1000d000",
            "arch": "${__CPU_ARCH__}"
          }
        ]
      },
      "context": {
        "flags": 986655,
        "yes": true,
        "nah": null
      },
      "_dd": {
        "format_version": 2
      }
    })"));

    // And no warning or errors are logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
    REQUIRE(diagnostics.status.size() > 0);
    REQUIRE_THAT(
        diagnostics.status.back(),
        Catch::Matchers::ContainsSubstring(
            "Handled crash report: created ApplicationLaunch view and recorded RUM "
            "Error in prior-process session"
        )
    );
  }

  SECTION(
      "M disregard crash W no view was active and application was not in "
      "initial-session-with-no-views state"
  ) {
    // Given a crash in a process with no active views, but in a state where either
    // we've created views or we're no longer in the initial session
    auto crash = make_crash_report();
    auto permutation = GENERATE(0, 1, 2);
    switch (permutation) {
      case 0:
        crash.context->rum_session_state.is_initial_session = true;
        crash.context->rum_session_state.has_tracked_any_view = true;
        break;
      case 1:
        crash.context->rum_session_state.is_initial_session = false;
        crash.context->rum_session_state.has_tracked_any_view = false;
        break;
      case 2:
      default:
        crash.context->rum_session_state.is_initial_session = false;
        crash.context->rum_session_state.has_tracked_any_view = true;
        break;
    }

    // When we process that crash
    auto deps = make_deps();
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // And a warning is logged to signal that we don't handle crashes that occurred
    // while no RUM View was active (except in the case of ApplicationLaunch)
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 1);
    REQUIRE_THAT(
        diagnostics.warning[0],
        Catch::Matchers::ContainsSubstring(
            "Ignoring prior-process crash report: crash occurred while no RUM View was "
            "active"
        )
    );
  }

  SECTION(
      "M produce updated view event and new error event W view was active and crash "
      "occurred less than 4 hours ago"
  ) {
    // Given a crash report with an active view
    auto last_view = make_last_view_event();
    auto crash = make_crash_report(last_view);

    // And regardless of whether the CrashContext includes up-to-date RumSessionState
    // matching that view
    auto with_outdated_session_state = GENERATE(false, true);
    if (with_outdated_session_state) {
      // This simulates the case where a crash occurs after the last view event is
      // flushed but before corresponding changes in session state can be handled,
      // allowing us to validate that we give precedence to the application and session
      // details parsed from the view event
      auto with_previous_session = GENERATE(false, true);
      if (with_previous_session) {
        crash.context->rum_session_state.session_id =
            *UUID::Parse("44444444-4444-4444-4444-444444444444");
      } else {
        crash.context->rum_session_state.session_id = UUID::Zero;
      }
    }

    // When we process that crash shortly after it's reported
    auto deps = make_deps();
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // Then we produce a schema-compliant RUM View event that describes the original
    // view, updated to reflect that the crash occurred
    REQUIRE(new_view_event.is_object());
    RequireEventMatch(new_view_event, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "view",
      "date": 1699999989999,
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
        "name": "My View",
        "is_active": false,
        "time_spent": 5000000000,
        "action": {"count": 2},
        "error": {"count": 4},
        "crash": {"count": 1},
        "resource": {"count": 1}
      },
      "context": {
        "flags": 986655,
        "yes": true,
        "nah": null
      },
      "_dd": {
        "format_version": 2,
        "document_version": 13
      }
    })"));

    // And we also produce a schema-compliant RUM Error event that describes the crash
    // while also conveying the relevant details from both the view and the CrashContext
    REQUIRE(new_error_event.is_object());
    RequireEventMatch(new_error_event, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "error",
      "date": 1699999990000,
      "service": "my-service",
      "version": "1.0.0",
      "os": {
        "name": "CoolOS",
        "version": "3.3.3",
        "build": "bbbbc001",
        "version_major": "3"
      },
      "device": {
        "type": "desktop",
        "name": "Elektronika BK-84M",
        "model": "БК-84М Спектр",
        "brand": "NPO Elektronika",
        "architecture": "PDP-11",
        "locale": "tk-TM",
        "time_zone": "Asia/Ashgabat"
      },
      "usr": {
        "id": "u-13043904",
        "name": "Important User",
        "email": "person@example.com",
        "middle_initial": "Q"
      },
      "account": {
        "id": "fujda9i8z83457",
        "name": "Important Account",
        "service_tier": "good",
        "ids": [8675309, 2813308004]
      },
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
        "name": "My View"
      },
      "error": {
        "id": "${__NONZERO_UUID__}",
        "is_crash": true,
        "message": "${__ERROR_MESSAGE_APPLICATION_CRASH__}",
        "source": "source",
        "source_type": "${__ERROR_SOURCE_TYPE_PLATFORM_NAME__}",
        "stack": "0   some-other-library\t0x000000001000c100 0x1000c000 + 256\n1   some-other-library\t0x000000001000c50f 0x1000c000 + 1295\n2   my-cool-program\t0x0000000010004000 0x10000000 + 16384\n3   my-cool-program\t0x0000000010001000 0x10000000 + 4096\n4   ???\t0x000000001000bbbb 0x0 + 0\n5   some-system-library\t0x000000001000aaaa 0x1000a000 + 2730\n",
        "binary_images": [
          {
            "uuid": "build-id-1",
            "name": "my-cool-program",
            "is_system": false,
            "load_address": "10000000",
            "max_address": "1000a000",
            "arch": "${__CPU_ARCH__}"
          },
          {
            "uuid": "build-id-2",
            "name": "some-system-library",
            "is_system": true,
            "load_address": "1000a000",
            "max_address": "1000b000",
            "arch": "${__CPU_ARCH__}"
          },
          {
            "uuid": "build-id-3",
            "name": "some-other-library",
            "is_system": false,
            "load_address": "1000c000",
            "max_address": "1000d000",
            "arch": "${__CPU_ARCH__}"
          }
        ]
      },
      "context": {
        "flags": 986655,
        "yes": true,
        "nah": null
      },
      "_dd": {
        "format_version": 2
      }
    })"));

    // And no warnings or errors are logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION(
      "M produce new error event W view was active but crash occurred over 4 hours ago"
  ) {
    // Given a crash report with an active view
    auto last_view = make_last_view_event();
    auto crash = make_crash_report(last_view);

    // And regardless of whether the CrashContext includes up-to-date RumSessionState
    // matching that view
    auto with_outdated_session_state = GENERATE(false, true);
    if (with_outdated_session_state) {
      crash.context->rum_session_state.session_id = UUID::Zero;
    }

    // When we process that crash after more than 4 hours have elapsed since the crash
    // (crash happened at 1699999990000ms; initial MockClock time was 1700000000000ms)
    clock.Tick(std::chrono::hours(4));
    auto deps = make_deps();
    ContextThread_HandleCrashReport(deps, crash, event_writer);

    // Then we produce no view event
    REQUIRE(new_view_event.is_null());

    // And we produce an error event that's identical to the event we would have
    // produced in the case where we _did_ update the view
    REQUIRE(new_error_event.is_object());
    RequireEventMatch(new_error_event, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "error",
      "date": 1699999990000,
      "service": "my-service",
      "version": "1.0.0",
      "os": {
        "name": "CoolOS",
        "version": "3.3.3",
        "build": "bbbbc001",
        "version_major": "3"
      },
      "device": {
        "type": "desktop",
        "name": "Elektronika BK-84M",
        "model": "БК-84М Спектр",
        "brand": "NPO Elektronika",
        "architecture": "PDP-11",
        "locale": "tk-TM",
        "time_zone": "Asia/Ashgabat"
      },
      "usr": {
        "id": "u-13043904",
        "name": "Important User",
        "email": "person@example.com",
        "middle_initial": "Q"
      },
      "account": {
        "id": "fujda9i8z83457",
        "name": "Important Account",
        "service_tier": "good",
        "ids": [8675309, 2813308004]
      },
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
        "name": "My View"
      },
      "error": {
        "id": "${__NONZERO_UUID__}",
        "is_crash": true,
        "message": "${__ERROR_MESSAGE_APPLICATION_CRASH__}",
        "source": "source",
        "source_type": "${__ERROR_SOURCE_TYPE_PLATFORM_NAME__}",
        "stack": "0   some-other-library\t0x000000001000c100 0x1000c000 + 256\n1   some-other-library\t0x000000001000c50f 0x1000c000 + 1295\n2   my-cool-program\t0x0000000010004000 0x10000000 + 16384\n3   my-cool-program\t0x0000000010001000 0x10000000 + 4096\n4   ???\t0x000000001000bbbb 0x0 + 0\n5   some-system-library\t0x000000001000aaaa 0x1000a000 + 2730\n",
        "binary_images": [
          {
            "uuid": "build-id-1",
            "name": "my-cool-program",
            "is_system": false,
            "load_address": "10000000",
            "max_address": "1000a000",
            "arch": "${__CPU_ARCH__}"
          },
          {
            "uuid": "build-id-2",
            "name": "some-system-library",
            "is_system": true,
            "load_address": "1000a000",
            "max_address": "1000b000",
            "arch": "${__CPU_ARCH__}"
          },
          {
            "uuid": "build-id-3",
            "name": "some-other-library",
            "is_system": false,
            "load_address": "1000c000",
            "max_address": "1000d000",
            "arch": "${__CPU_ARCH__}"
          }
        ]
      },
      "context": {
        "flags": 986655,
        "yes": true,
        "nah": null
      },
      "_dd": {
        "format_version": 2
      }
    })"));

    // And no warnings or errors are logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }
}
