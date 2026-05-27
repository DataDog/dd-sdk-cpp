// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/logging/event_generation.hpp"

#include <cinttypes>
#include <vector>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/logging/data.hpp"

#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("ContextThread_GenerateLogEvent", "[unit][logging]") {
  // Given an EventWriter function that will capture all JSON objects produced by our
  // event-generation routine, failing if we produce anything other than a JSON object
  std::vector<nlohmann::json> events;
  EventWriter event_writer =
      [&](Block event_data, Block event_metadata, bool bypass_tracking_consent) {
        // Loggers don't generate event metadata or bypass tracking consent
        REQUIRE(bypass_tracking_consent == false);
        REQUIRE(event_metadata.size() == 0);

        // Log events are valid JSON objects
        auto obj = nlohmann::json::parse(event_data);
        REQUIRE(obj.is_object());

        // Capture the event so our test can examine it
        events.push_back(obj);
        return true;
      };

  // And basic config details from a Logger, which individual test cases can modify
  LoggerConfigDetails logger{"my-overridden-service", "my-logger", true};

  // And the basic details of a log call, which can also be freely modified
  LogCallDetails call{
      LogLevel::Info,
      "Hello, this is a log message",
      "",
      "",
      Timestamp{std::chrono::nanoseconds(1779463265013148820)},
      Attribute(),
      ""
  };

  // And a CoreContext snapshot
  CoreContext ctx = MOCK_CONTEXT;

  // And a buffer that can be reused for JSON serialization of event payloads
  std::vector<uint8_t> buf;

  // And a no-op MessagePublisher for test cases that are not concerned with bus
  // messages
  MessagePublisher noop_publisher = [](FeatureMessage) { return true; };

  SECTION("M produce a valid JSON-encoded LogEvent") {
    // When we attempt to generate a log event from our default set of test values
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );

    // Then we produce a single JSON-encoded LogEvent with all relevant details
    REQUIRE(events.size() == 1);
    RequireEventMatch(events[0], R"({
      "status": "info",
      "service": "my-overridden-service",
      "date": "2026-05-22T15:21:05.013Z",
      "message": "Hello, this is a log message",
      "ddtags": "service:my-overridden-service,env:mock-env,sdk_version:1.2.3",
      "logger.name": "my-logger",
      "logger.version": "1.2.3",
      "os": {
        "name": "mock-os",
        "version": "2.3.4",
        "build": "mock-build-number",
        "version_major": "2"
      },
      "device": {
        "type": "desktop",
        "name": "mock-device",
        "model": "mock-model",
        "brand": "mock-brand",
        "architecture": "x86_64",
        "locale": "en-US",
        "time_zone": "America/New_York"
      }
    })");
  }

  SECTION("M set status to reflect log level") {
    // Given a range of LogLevel values
    struct TestParams {
      LogLevel level;
      std::string want_status;
    };
    auto t = GENERATE(
        values<TestParams>(
            {{LogLevel::Debug, "debug"},
             {LogLevel::Info, "info"},
             {LogLevel::Notice, "notice"},
             {LogLevel::Warn, "warn"},
             {LogLevel::Error, "error"},
             {LogLevel::Critical, "critical"}}
        )
    );
    CAPTURE(t.level, t.want_status);

    // When we generate a log event at that level
    call.level = t.level;
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );

    // Then the resulting 'status' value is set accordingly
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["status"] == t.want_status);
  }

  SECTION("M use default service name W no per-logger override is configured") {
    // When the Logger has no service-name override configured
    logger.service_override = "";
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );

    // Then the event carries the default service name configured for the SDK instance,
    // and ddtags reflects the same SDK-level service name
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["service"] == "mock-service");
    REQUIRE(
        events[0]["ddtags"] == "service:mock-service,env:mock-env,sdk_version:1.2.3"
    );
  }

  SECTION("M append tags to ddtags W custom tag values are present") {
    // When the log call includes a non-empty snapshot of custom tags applied to the
    // logger
    call.logger_tags = "foo:hello1,bar";
    ContextThread_GenerateLogEvent(logger, call, ctx, event_writer, buf);

    // Then the event carries a ddtags value where our custom logger tags are
    // concatenated after the internal values
    REQUIRE(events.size() == 1);
    REQUIRE(
        events[0]["ddtags"] ==
        "service:my-overridden-service,env:mock-env,sdk_version:1.2.3,foo:hello1,bar"
    );
  }

  SECTION(
      "M append tags to ddtags W custom tag values are present and service name is not "
      "overridden"
  ) {
    // When the log call includes a non-empty snapshot of custom tags applied to the
    // logger, and the service name uses the default value
    logger.service_override = "";
    call.logger_tags = "foo:hello1,bar";
    ContextThread_GenerateLogEvent(logger, call, ctx, event_writer, buf);

    // Then the event carries a ddtags value where our custom logger tags are
    // concatenated after the internal values
    REQUIRE(events.size() == 1);
    REQUIRE(
        events[0]["ddtags"] ==
        "service:mock-service,env:mock-env,sdk_version:1.2.3,foo:hello1,bar"
    );
  }

  SECTION("M format date as ISO timestamp") {
    // When the log call is timestamped with a value equivalent to 59 seconds,
    // indicating 59 seconds after midnight on January 1, 1970, UTC
    call.timestamp = Timestamp{std::chrono::seconds(59)};
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );

    // Then the resulting 'date' value reflects that point in time
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["date"] == "1970-01-01T00:00:59.000Z");
  }

  SECTION("M allow any string value as message") {
    // Given a variety of string values, including ASCII control codes, UTF-8, and an
    // empty string
    auto message = GENERATE("hi", "dobrý deň 🌞", "\t\n\u0007! ", "");
    CAPTURE(message);

    // When we generate a log event using that message
    call.message = message;
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );

    // Then the resulting 'message' value contains it exactly as written, and the
    // message property is never omitted, even if blank
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["message"] == message);
  }

  SECTION("M omit logger.name W logger has no configured name") {
    // When we generate a log event from a logger which has no specified name
    logger.name = "";
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );

    // Then the resulting message has no 'logger.name' value. Note that iOS and Android
    // SDKs auto-populate this field with an application identifier, for which there is
    // no exact analogue in the C++ SDK. We could roughly replicate this behavior by
    // grabbing basename(argv[0]) on launch and stashing it in CoreContext.
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["message"] == "Hello, this is a log message");
    REQUIRE(!events[0].contains("logger.name"));
  }

  SECTION("M omit os W no OsInfo has been resolved") {
    // When we have no valid OsInfo pointer in CoreContext, Then 'os' is omitted
    ctx.os = nullptr;
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["message"] == "Hello, this is a log message");
    REQUIRE(!events[0].contains("os"));
  }

  SECTION("M omit device W no DeviceInfo has been resolved") {
    // When we have no valid DeviceInfo pointer in CoreContext, Then 'device' is omitted
    ctx.device = nullptr;
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["message"] == "Hello, this is a log message");
    REQUIRE(!events[0].contains("device"));
  }

  SECTION(
      "M merge custom attributes into JSON object at top level W merged_attributes has "
      ">0 properties"
  ) {
    // When LogCall::merged_attributes is an object with one or more properties
    call.merged_attributes.InitObject(2);
    call.merged_attributes.SetObjectProperty("foo", Attribute::Int(100));
    call.merged_attributes.SetObjectProperty("bar", Attribute::String("hello"));
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );

    // Then the resulting JSON object includes those custom values as top-level
    // properties
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["foo"] == 100);
    REQUIRE(events[0]["bar"] == "hello");
  }

  SECTION("{RUM context enrichment}") {
    // Given that our default logger config has RUM enrichment enabled
    REQUIRE(logger.enrich_with_rum_context);

    SECTION("M not include RUM ids W ctx.rum has value but all IDs are zero") {
      // When CoreContext has a valid RumFeatureContext, but all its UUIDs are unset
      ctx.rum.emplace();

      // Then our event carries no RUM IDs
      ContextThread_GenerateLogEvent(
          logger, call, ctx, event_writer, buf, noop_publisher
      );
      REQUIRE(events.size() == 1);
      REQUIRE(!events[0].contains("application_id"));
      REQUIRE(!events[0].contains("session_id"));
      REQUIRE(!events[0].contains("view.id"));
      REQUIRE(!events[0].contains("user_action.id"));
    }

    SECTION("M include application_id, session_id W RUM has active session") {
      // When CoreContext reflects an active RUM Session
      ctx.rum.emplace();
      ctx.rum->application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
      ctx.rum->session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");

      // Then our event carries these two RUM IDs
      ContextThread_GenerateLogEvent(
          logger, call, ctx, event_writer, buf, noop_publisher
      );
      REQUIRE(events.size() == 1);
      const auto& ev = events[0];
      REQUIRE(ev["application_id"] == "a991ca10-4004-4004-4004-beefbeefbeef");
      REQUIRE(ev["session_id"] == "5e551017-4114-4114-4114-beeeefbeeeef");
      REQUIRE(!ev.contains("view.id"));
      REQUIRE(!ev.contains("user_action.id"));
    }

    SECTION("M include application_id, session_id, view.id W RUM has active view") {
      // When CoreContext reflects an active RUM View
      ctx.rum.emplace();
      ctx.rum->application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
      ctx.rum->session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
      ctx.rum->view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");

      // Then our event carries these three RUM IDs
      ContextThread_GenerateLogEvent(
          logger, call, ctx, event_writer, buf, noop_publisher
      );
      REQUIRE(events.size() == 1);
      const auto& ev = events[0];
      REQUIRE(ev["application_id"] == "a991ca10-4004-4004-4004-beefbeefbeef");
      REQUIRE(ev["session_id"] == "5e551017-4114-4114-4114-beeeefbeeeef");
      REQUIRE(ev["view.id"] == "141ee144-4224-4224-4224-beeeeeeeeeef");
      REQUIRE(!ev.contains("user_action.id"));
    }

    SECTION(
        "M include application_id, session_id, view.id, user_action.id W RUM has "
        "active action"
    ) {
      // When CoreContext reflects an active RUM Action
      ctx.rum.emplace();
      ctx.rum->application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
      ctx.rum->session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
      ctx.rum->view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
      ctx.rum->action_id = *UUID::Parse("4c10171e-4334-4334-4334-b0000eeeefff");

      // Then our event carries these four RUM IDs
      ContextThread_GenerateLogEvent(
          logger, call, ctx, event_writer, buf, noop_publisher
      );
      REQUIRE(events.size() == 1);
      const auto& ev = events[0];
      REQUIRE(ev["application_id"] == "a991ca10-4004-4004-4004-beefbeefbeef");
      REQUIRE(ev["session_id"] == "5e551017-4114-4114-4114-beeeefbeeeef");
      REQUIRE(ev["view.id"] == "141ee144-4224-4224-4224-beeeeeeeeeef");
      REQUIRE(ev["user_action.id"] == "4c10171e-4334-4334-4334-b0000eeeefff");
    }

    SECTION("M not include RUM ids W enrich_with_rum_context is disabled") {
      // When CoreContext indicates that RUM state is very much active
      ctx.rum.emplace();
      ctx.rum->application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
      ctx.rum->session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
      ctx.rum->view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
      ctx.rum->action_id = *UUID::Parse("4c10171e-4334-4334-4334-b0000eeeefff");

      // But enrich_with_rum_context is disabled
      logger.enrich_with_rum_context = false;

      // Then our event carries no RUM IDs
      ContextThread_GenerateLogEvent(
          logger, call, ctx, event_writer, buf, noop_publisher
      );
      REQUIRE(events.size() == 1);
      REQUIRE(!events[0].contains("application_id"));
      REQUIRE(!events[0].contains("session_id"));
      REQUIRE(!events[0].contains("view.id"));
      REQUIRE(!events[0].contains("user_action.id"));
    }
  }

  SECTION("M include usr W CoreContext contains user info") {
    // When CoreContext includes non-empty user info
    ctx.user_info.id = "user-123";
    ctx.user_info.name = "Jane Doe";
    ctx.user_info.email = "jane@example.com";
    ctx.user_info.extra = Attribute::Object(1);
    ctx.user_info.extra.SetObjectProperty("foo", Attribute::Int(5678));

    // Then our event carries that information in 'usr'
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );
    REQUIRE(events.size() == 1);
    REQUIRE(
        events[0]["usr"] == nlohmann::json{
                                {"id", "user-123"},
                                {"name", "Jane Doe"},
                                {"email", "jane@example.com"},
                                {"foo", 5678}
                            }
    );
  }

  SECTION("M include account W CoreContext contains account info") {
    // When CoreContext includes non-empty account info
    ctx.account_info.id = "account-456";
    ctx.account_info.name = "Important Account";
    ctx.account_info.extra = Attribute::Object(1);
    ctx.account_info.extra.SetObjectProperty("foo", Attribute::Int(9876));

    // Then our event carries that information in 'account'
    ContextThread_GenerateLogEvent(
        logger, call, ctx, event_writer, buf, noop_publisher
    );
    REQUIRE(events.size() == 1);
    REQUIRE(
        events[0]["account"] ==
        nlohmann::json{
            {"id", "account-456"}, {"name", "Important Account"}, {"foo", 9876}
        }
    );
  }

  SECTION("M include error.kind and error.stack W both are provided") {
    // When a log call carries non-empty error_kind and error_stack
    call.error_kind = "SomeException";
    call.error_stack = "frame 0\nframe 1";
    ContextThread_GenerateLogEvent(logger, call, ctx, event_writer, buf);

    // Then the resulting event carries both error fields
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["error.kind"] == "SomeException");
    REQUIRE(events[0]["error.stack"] == "frame 0\nframe 1");
  }

  SECTION("M omit error fields W both are empty") {
    // When a log call carries empty error_kind and error_stack (the default)
    REQUIRE(call.error_kind.empty());
    REQUIRE(call.error_stack.empty());
    ContextThread_GenerateLogEvent(logger, call, ctx, event_writer, buf);

    // Then the resulting event carries neither error field
    REQUIRE(events.size() == 1);
    REQUIRE(!events[0].contains("error.kind"));
    REQUIRE(!events[0].contains("error.stack"));
  }

  SECTION("M publish LoggerErrorMessage W level is error or critical") {
    // Given a capturing publisher
    std::vector<FeatureMessage> messages;
    MessagePublisher publisher = [&](FeatureMessage msg) {
      messages.push_back(std::move(msg));
      return true;
    };

    // And a set of user attributes on the call
    call.merged_attributes.InitObject(1);
    call.merged_attributes.SetObjectProperty("key", Attribute::String("value"));

    // When we generate events at error and critical level
    auto error_call = call;
    error_call.level = LogLevel::Error;
    error_call.message = "something went wrong";
    ContextThread_GenerateLogEvent(
        logger, error_call, ctx, event_writer, buf, publisher
    );

    auto critical_call = call;
    critical_call.level = LogLevel::Critical;
    critical_call.message = "fatal problem";
    ContextThread_GenerateLogEvent(
        logger, critical_call, ctx, event_writer, buf, publisher
    );

    // Then each call produces one LoggerErrorMessage on the bus
    REQUIRE(messages.size() == 2);

    const auto* first = std::get_if<LoggerErrorMessage>(&messages[0]);
    REQUIRE(first != nullptr);
    REQUIRE(first->message == "something went wrong");
    REQUIRE(first->timestamp == call.timestamp);

    const auto* second = std::get_if<LoggerErrorMessage>(&messages[1]);
    REQUIRE(second != nullptr);
    REQUIRE(second->message == "fatal problem");
  }

  SECTION("M not publish LoggerErrorMessage W level is below error") {
    // Given a capturing publisher
    std::vector<FeatureMessage> messages;
    MessagePublisher publisher = [&](FeatureMessage msg) {
      messages.push_back(std::move(msg));
      return true;
    };

    // When we generate events at levels below error
    auto t =
        GENERATE(LogLevel::Debug, LogLevel::Info, LogLevel::Notice, LogLevel::Warn);
    CAPTURE(t);
    call.level = t;
    ContextThread_GenerateLogEvent(logger, call, ctx, event_writer, buf, publisher);

    // Then no LoggerErrorMessage is published
    REQUIRE(messages.empty());
  }
}
