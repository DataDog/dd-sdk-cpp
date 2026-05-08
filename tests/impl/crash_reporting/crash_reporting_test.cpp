// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/crash_reporting.hpp"

#include <chrono>
#include <memory>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/storage/path.hpp"

#include "mock/filesystem.hpp"
#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/crash_data.hpp"
#include "support/crash_handler.hpp"
#include "support/feature.hpp"

using namespace datadog::impl;

TEST_CASE("CrashReporting message handling", "[unit][crash_reporting]") {
  // Given a mock CoreContext value that will be copied into messages
  CoreContext context = MOCK_CONTEXT;

  // And some required ICrashHandler dependencies that are not exercised in this test
  MockFilesystem fs;
  StoragePath path;
  REQUIRE(path.Set("unused-path"));

  // And a mock ICrashHandler injected into a valid CrashReporting feature
  MockCrashHandler handler;
  auto crash_reporting =
      std::make_shared<datadog::impl::CrashReporting>(handler, fs, path);
  REQUIRE(crash_reporting);

  // And a message handler callback returned by the feature implementation
  auto message_handler = crash_reporting->MakeMessageHandler();
  REQUIRE(message_handler.has_value());
  auto message_handler_func = *message_handler;
  REQUIRE(message_handler_func);

  SECTION("M convey SDK context to handler W ContextChangedMessage is received") {
    // When the feature handles a ContextChangedMessage
    message_handler_func(ContextChangedMessage{context});

    // Then our handler has been notified with the context fields populated from
    // MOCK_CONTEXT
    REQUIRE(handler.num_set_crash_context_calls == 1);
    REQUIRE(handler.last_crash_ctx.has_value());
    REQUIRE(handler.last_crash_ctx->service == "mock-service");
    REQUIRE(handler.last_crash_ctx->env == "mock-env");
    REQUIRE(handler.last_crash_ctx->os_name == "mock-os");
    REQUIRE(handler.last_crash_ctx->device_type == "desktop");

    SECTION("M convey updated context W ContextChangedMessage carries any change") {
      // When another message arrives with a changed field
      context.tracking_consent = datadog::TrackingConsent::Granted;
      message_handler_func(ContextChangedMessage{context});

      // Then a second SetCrashContext call was made reflecting the update
      REQUIRE(handler.num_set_crash_context_calls == 2);
      REQUIRE(
          handler.last_crash_ctx->tracking_consent == datadog::TrackingConsent::Granted
      );
    }
  }

  SECTION("M update session state W RumSessionStateChangedMessage is received") {
    // When the feature handles a RumSessionStateChangedMessage with a new session
    RumSessionState state{};
    state.session_id = *datadog::UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
    state.is_sampled = true;
    state.is_active = true;
    message_handler_func(RumSessionStateChangedMessage{state});

    // Then the handler receives the updated session state
    REQUIRE(handler.num_set_crash_context_calls == 1);
    REQUIRE(handler.last_crash_ctx.has_value());
    REQUIRE(
        handler.last_crash_ctx->rum_session_state.session_id ==
        *datadog::UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef")
    );
    REQUIRE(handler.last_crash_ctx->rum_session_state.is_sampled);
    REQUIRE(handler.last_crash_ctx->rum_session_state.is_active);
  }

  SECTION("M serialize active view W RumActiveViewUpdatedMessage is received") {
    // Given a minimal RumViewEvent
    const datadog::Timestamp date{std::chrono::nanoseconds(946684799999999999)};
    const datadog::UUID app_id =
        *datadog::UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
    const datadog::UUID session_id =
        *datadog::UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
    const datadog::UUID view_id =
        *datadog::UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
    RumViewEvent view_event{
        date,
        app_id,
        session_id,
        RumSessionType::User,
        view_id,
        "my-view",
        0,
        0,
        0,
        0,
        0
    };

    // When the feature handles a RumActiveViewUpdatedMessage
    message_handler_func(RumActiveViewUpdatedMessage{view_event});

    // Then the handler is notified and last_view_event_json is populated
    REQUIRE(handler.num_set_crash_context_calls == 1);
    REQUIRE(handler.last_crash_ctx.has_value());
    REQUIRE(!handler.last_crash_ctx->last_view_event_json.empty());

    SECTION("M clear view JSON W subsequent RumActiveViewLostMessage is received") {
      // When the active view is then lost
      message_handler_func(RumActiveViewLostMessage{});

      // Then the handler is notified and last_view_event_json is cleared
      REQUIRE(handler.num_set_crash_context_calls == 2);
      REQUIRE(handler.last_crash_ctx->last_view_event_json.empty());
    }
  }

  SECTION(
      "M clear view JSON W RumActiveViewLostMessage is received with no prior view"
  ) {
    // When a lost-view message is received without a prior view update
    message_handler_func(RumActiveViewLostMessage{});

    // Then the handler is notified (even if nothing changed)
    REQUIRE(handler.num_set_crash_context_calls == 1);
    REQUIRE(handler.last_crash_ctx.has_value());
    REQUIRE(handler.last_crash_ctx->last_view_event_json.empty());
  }

  SECTION(
      "M serialize global attributes W RumGlobalAttributesChangedMessage is received"
  ) {
    // When the feature handles a RumGlobalAttributesChangedMessage with some attributes
    message_handler_func(RumGlobalAttributesChangedMessage{datadog::Attribute{}});

    // Then the handler is notified and global_rum_attributes holds the sent value
    REQUIRE(handler.num_set_crash_context_calls == 1);
    REQUIRE(handler.last_crash_ctx.has_value());
    REQUIRE(
        handler.last_crash_ctx->global_rum_attributes.GetType() ==
        datadog::ValueType::Null
    );
  }

  SECTION("M do nothing W CrashReportProcessedMessage is received") {
    // When the feature handles a CrashReportProcessedMessage (originating from itself)
    message_handler_func(CrashReportProcessedMessage{CrashReport{}});

    // Then no SetCrashContext call is made: this message does not affect crash context
    REQUIRE(handler.num_set_crash_context_calls == 0);
  }

  SECTION("M safely do nothing W message arrives after feature is destroyed") {
    // When the last std::shared_ptr<CrashReporting> is destroyed
    crash_reporting.reset();

    // And a lingering message-handler callback receives a message
    REQUIRE(message_handler_func);
    message_handler_func(ContextChangedMessage{context});

    // Then nothing happens: the weak_ptr check prevents use-after-free
    REQUIRE(handler.num_set_crash_context_calls == 0);
    REQUIRE(!handler.last_crash_ctx.has_value());
  }
}

TEST_CASE("CrashReporting message publishing", "[unit][crash_reporting]") {
  // Given crash file binary data that test sections can write to the mock filesystem
  static const std::string_view CRASH_FILE_DATA{
      reinterpret_cast<const char*>(MOCK_CRASH_REPORT_V1),
      std::size(MOCK_CRASH_REPORT_V1)
  };

  // And a mock filesystem and storage path that test sections can populate with crash
  // files as needed
  MockFilesystem fs;
  StoragePath path;
  REQUIRE(path.Set("app/.datadog/.crashes"));

  // And a CrashReporting feature backed by the mock filesystem
  MockCrashHandler handler;
  auto crash_reporting =
      std::make_shared<datadog::impl::CrashReporting>(handler, fs, path);

  // And a FeatureTest harness that will capture any messages the feature publishes
  FeatureTest feature_test(MOCK_CONTEXT);

  SECTION("M publish CrashReportProcessedMessage W valid crash file exists on start") {
    // Given a crash storage directory containing a valid crash dump
    fs.Mkdirs("app/.datadog/.crashes");
    fs.Touch("app/.datadog/.crashes/crash_1700000000000_12345", CRASH_FILE_DATA);

    // When the feature starts (FeatureTest processes all context-thread work serially)
    feature_test.Start(crash_reporting);

    // Then exactly one CrashReportProcessedMessage is published to the message bus
    REQUIRE(feature_test.feature_messages.size() == 1);
    const auto* msg =
        std::get_if<CrashReportProcessedMessage>(&feature_test.feature_messages[0]);
    REQUIRE(msg != nullptr);

    // And the message contains the crash data parsed from the file
    REQUIRE(msg->crash.fault_code == 11);
    REQUIRE(msg->crash.timestamp == 1700000000000);
  }

  SECTION("M publish no messages W no crash files exist on start") {
    // Given an empty crash storage directory
    fs.Mkdirs("app/.datadog/.crashes");

    // When the feature starts
    feature_test.Start(crash_reporting);

    // Then no messages are published
    REQUIRE(feature_test.feature_messages.empty());
  }
}
