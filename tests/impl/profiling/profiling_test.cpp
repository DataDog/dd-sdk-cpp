// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profiling.hpp"

#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "datadog/impl/core/feature_message.hpp"

#include "mock/profiler.hpp"
#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/feature.hpp"

using namespace datadog::impl;

TEST_CASE("Profiling feature lifecycle", "[unit][profiling]") {
  // Given a mock profiler, keeping a reference to it before the feature takes ownership
  auto mock_profiler = std::make_unique<MockProfiler>();
  MockProfiler& profiler = *mock_profiler;

  // And a Profiling feature that owns the profiler
  ProfilerSettings settings{"my-service", "staging", "1.2.3"};
  auto profiling =
      std::make_shared<impl::Profiling>(std::move(mock_profiler), std::move(settings));

  // And a message handler callback returned by the feature implementation
  auto message_handler = profiling->MakeMessageHandler();
  REQUIRE(message_handler.has_value());
  auto message_handler_func = *message_handler;
  REQUIRE(message_handler_func);

  // And a function that delivers a context update with the given tracking consent, as
  // the Core does whenever its context changes
  CoreContext context = MOCK_CONTEXT;
  auto send_consent = [&](datadog::TrackingConsent consent) {
    context.tracking_consent = consent;
    message_handler_func(ContextChangedMessage{context});
  };

  // And a harness that operates the feature as a running Core would
  FeatureTest test(context);

  SECTION("M not start the profiler W the core starts and consent has not been seen") {
    // When the core starts, without any context update having arrived
    test.Start(profiling);

    // Then the profiler is untouched
    REQUIRE(profiler.init_calls == 0);
    REQUIRE(profiler.start_calls == 0);
  }

  SECTION(
      "M configure and start the profiler W consent is granted after the core starts"
  ) {
    // When the core starts, then consent is granted
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // Then the profiler is configured with the app's identity, then started
    REQUIRE(profiler.init_calls == 1);
    REQUIRE(profiler.start_calls == 1);
    REQUIRE(profiler.last_settings.service == "my-service");
    REQUIRE(profiler.last_settings.env == "staging");
    REQUIRE(profiler.last_settings.version == "1.2.3");
    REQUIRE(test.diagnostic_messages.empty());
  }

  SECTION("M start the profiler W the core starts after consent is granted") {
    // When consent is granted before the core's Start() is called
    send_consent(datadog::TrackingConsent::Granted);
    REQUIRE(profiler.start_calls == 0);
    test.Start(profiling);

    // Then the profiler starts once the core does
    REQUIRE(profiler.init_calls == 1);
    REQUIRE(profiler.start_calls == 1);
  }

  SECTION("M not start the profiler W consent is pending or not granted") {
    // When consent is anything other than granted
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Pending);
    send_consent(datadog::TrackingConsent::NotGranted);

    // Then the profiler is never touched
    REQUIRE(profiler.init_calls == 0);
    REQUIRE(profiler.start_calls == 0);
    REQUIRE(profiler.stop_calls == 0);
  }

  SECTION("M stop the profiler W consent is withdrawn") {
    // When consent is granted, then revoked
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    send_consent(datadog::TrackingConsent::NotGranted);

    // Then the profiler is stopped
    REQUIRE(profiler.stop_calls == 1);
  }

  SECTION("M restart the profiler W consent is granted again") {
    // When consent is granted, revoked, then granted again
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    send_consent(datadog::TrackingConsent::NotGranted);
    send_consent(datadog::TrackingConsent::Granted);

    // Then the profiler has been set up and started twice, and stopped once
    REQUIRE(profiler.init_calls == 2);
    REQUIRE(profiler.start_calls == 2);
    REQUIRE(profiler.stop_calls == 1);
  }

  SECTION("M stop the profiler W the core stops") {
    // When the core starts, consent is granted, and the core stops
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    test.Stop(profiling);

    // Then the profiler is stopped
    REQUIRE(profiler.stop_calls == 1);
  }

  SECTION("M not start the profiler W consent arrives after the core stops") {
    // When a late context update arrives after the core has stopped
    test.Start(profiling);
    test.Stop(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // Then the profiler is not started
    REQUIRE(profiler.init_calls == 0);
    REQUIRE(profiler.start_calls == 0);
  }

  SECTION("M start and stop the profiler for each run W the core is restarted") {
    // When the core runs twice, with consent granted during each run
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    test.Stop(profiling);
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    test.Stop(profiling);

    // Then the profiler has been set up, started, and stopped once per run
    REQUIRE(profiler.init_calls == 2);
    REQUIRE(profiler.start_calls == 2);
    REQUIRE(profiler.stop_calls == 2);
    REQUIRE(test.diagnostic_messages.empty());
  }

  SECTION("M wait for consent W the core is restarted") {
    // When the core is restarted after a run in which consent was granted
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    test.Stop(profiling);
    test.Start(profiling);

    // Then the profiler isn't started again until the new run reports its consent
    REQUIRE(profiler.start_calls == 1);
    send_consent(datadog::TrackingConsent::Granted);
    REQUIRE(profiler.start_calls == 2);
  }

  SECTION("M warn and not start W init fails") {
    profiler.init_result = false;

    // When consent is granted but the profiler can't be configured
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // Then a warning is logged and the profiler is not started
    REQUIRE(profiler.start_calls == 0);
    REQUIRE(test.diagnostic_messages.size() == 1);
    REQUIRE(test.diagnostic_messages[0].level == datadog::DiagnosticLevel::Warning);
    REQUIRE(test.diagnostic_messages[0].text == "Failed to configure profiler");

    // And a later stop doesn't touch a profiler that never started
    test.Stop(profiling);
    REQUIRE(profiler.stop_calls == 0);
  }

  SECTION("M warn and not stop W start fails") {
    profiler.start_result = false;

    // When consent is granted but the profiler can't be started
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    test.Stop(profiling);

    // Then a warning is logged and the profiler is never stopped
    REQUIRE(profiler.init_calls == 1);
    REQUIRE(profiler.start_calls == 1);
    REQUIRE(test.diagnostic_messages.size() == 1);
    REQUIRE(test.diagnostic_messages[0].level == datadog::DiagnosticLevel::Warning);
    REQUIRE(test.diagnostic_messages[0].text == "Failed to start profiler");
    REQUIRE(profiler.stop_calls == 0);
  }

  SECTION(
      "M not retry the profiler W unrelated context updates follow a failed start"
  ) {
    profiler.start_result = false;

    // When a start fails, then further context updates arrive with unchanged consent
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    send_consent(datadog::TrackingConsent::Granted);
    send_consent(datadog::TrackingConsent::Granted);

    // Then the start isn't retried and the warning isn't repeated
    REQUIRE(profiler.start_calls == 1);
    REQUIRE(test.diagnostic_messages.size() == 1);
  }
}

TEST_CASE("Profiling feature profile metadata", "[unit][profiling]") {
  // Given a Profiling feature that owns a mock profiler
  auto profiling = std::make_shared<impl::Profiling>(
      std::make_unique<MockProfiler>(),
      ProfilerSettings{"my-service", "staging", "1.2.3"},
      nullptr
  );
  auto message_handler_func = *profiling->MakeMessageHandler();

  // And helpers that deliver consent, and RUM events, as the Core and RUM do
  CoreContext context = MOCK_CONTEXT;
  auto send_consent = [&](datadog::TrackingConsent consent) {
    context.tracking_consent = consent;
    message_handler_func(ContextChangedMessage{context});
  };
  const UUID long_task_id = UUID::Random();
  auto send_long_task = [&] {
    RumLongTaskGeneratedMessage message;
    message.context.ids.view_id = UUID::Random();
    message.long_task_id = long_task_id;
    message.start = datadog::Timestamp{std::chrono::seconds{1700000000}};
    message.duration = std::chrono::milliseconds{60};
    message_handler_func(message);
  };
  const ProfileTimeRange range{
      datadog::Timestamp{std::chrono::seconds{1700000000}},
      datadog::Timestamp{std::chrono::seconds{1700000060}}
  };

  FeatureTest test(context);
  test.Start(profiling);

  SECTION("M describe the RUM events seen W the profiler is running") {
    // Given a running profiler that has seen a long task
    send_consent(datadog::TrackingConsent::Granted);
    send_long_task();

    // When the profiler asks for the metadata of a profile
    const ProfileMetadata metadata = profiling->BuildProfileMetadata(range);

    // Then it describes the application and the long task
    const auto event = nlohmann::json::parse(metadata.event_json);
    REQUIRE(
        event["tags_profiler"].get<std::string>().find("service:mock-service") !=
        std::string::npos
    );
    REQUIRE(event["long_task.id"][0] == long_task_id.ToString());
    REQUIRE(nlohmann::json::parse(metadata.rum_events_json).size() == 1);
  }

  SECTION("M not record RUM events W the profiler is not running") {
    // When a long task is seen before consent is granted
    send_consent(datadog::TrackingConsent::Pending);
    send_long_task();
    send_consent(datadog::TrackingConsent::Granted);

    // Then the profile does not describe it
    const ProfileMetadata metadata = profiling->BuildProfileMetadata(range);
    REQUIRE_FALSE(nlohmann::json::parse(metadata.event_json).contains("long_task.id"));
  }

  SECTION("M forget the RUM events seen W the profiler stops") {
    // Given a running profiler that has seen a long task
    send_consent(datadog::TrackingConsent::Granted);
    send_long_task();

    // When consent is withdrawn, then granted again
    send_consent(datadog::TrackingConsent::NotGranted);
    send_consent(datadog::TrackingConsent::Granted);

    // Then the long task is not described in the next profile
    const ProfileMetadata metadata = profiling->BuildProfileMetadata(range);
    REQUIRE_FALSE(nlohmann::json::parse(metadata.event_json).contains("long_task.id"));
  }
}
