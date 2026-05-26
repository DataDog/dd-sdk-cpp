// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/logging/logger.hpp"

#include "datadog/impl/logging/logging.hpp"

#include "mock/clock.hpp"
#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/diagnostics.hpp"
#include "support/feature.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("Logger", "[unit][logging]") {
  // The context-thread logic used to generate log events is already exhaustively
  // unit-tested in event_generation_test.cpp, and the API-layer tests in
  // logging_c_api_test.cpp and logging_cpp_api_test.cpp have exhaustive end-to-end
  // coverage for Logger usage scenarios. These tests are intended to validate
  // golden-path implementation-layer usage.

  // Given a mock system clock
  MockClock clock;
  clock.FreezeAtMilliseconds(1700000000000);

  // And a DiagnosticLogger that will capture warnings about usage
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger diagnostic_logger = diagnostics.CreateTestLogger();

  // And a Logging feature implementation, along with a test harness that will operate
  // that feature as a running Core would
  auto logging = std::make_shared<impl::Logging>(clock);
  CoreContext ctx = MOCK_CONTEXT;
  FeatureTest test(ctx);
  test.Start(logging);

  // And a Logger initialized with a reference to the Logging feature
  auto logger = std::make_unique<impl::Logger>(
      LoggerConfig().SetRemoteLogThreshold(LogLevel::Warn).SetName("cool-logger"),
      logging
  );

  SECTION("M produce log events W Log is called") {
    // When we log an error message
    Attribute error_attributes = Attribute::Object(2);
    error_attributes.SetObjectProperty("foo", Attribute::Int(333));
    error_attributes.SetObjectProperty("bar", Attribute::Int(444));
    logger->Log(LogLevel::Error, "This is an error", error_attributes);

    // And update our SDK state, apply some custom attributes, add custom tags, and log
    // another message
    clock.TickMilliseconds(1100);
    test.UpdateContext([](CoreContext& ctx) {
      ctx.rum.emplace();
      ctx.rum->application_id = *UUID::Parse("0976f38a-ae45-4f7a-8436-0c98c227a7b3");
      ctx.rum->session_id = *UUID::Parse("ab45517b-40ae-4f90-8c8a-bff62c05a166");
      ctx.user_info.id = "u-123456";
    });
    logging->AddAttribute("foo", Attribute::Int(11));
    logging->AddAttribute("qux", Attribute::Int(22));
    logger->AddAttribute("bar", Attribute::Int(33));
    logger->AddAttribute("qux", Attribute::Int(44));
    logger->AddAttribute("arf", Attribute::Int(55));
    logger->AddTag("foo:hello", diagnostic_logger);
    logger->AddTag("bar", "world", diagnostic_logger);
    logger->AddTag("baz", diagnostic_logger);

    Attribute warning_attributes = Attribute::Object(2);
    warning_attributes.SetObjectProperty("bar", Attribute::Int(555));
    warning_attributes.SetObjectProperty("baz", Attribute::Int(666));
    logger->Log(LogLevel::Warn, "This is a warning", warning_attributes);

    // And then finally log a message below our configured threshold
    clock.TickMilliseconds(1100);
    logger->Log(LogLevel::Info, "This is an info message");

    // And then stop the simulated Core
    test.Stop(logging);

    // Then we end up with two events
    REQUIRE(test.events.size() == 2);

    // And the first matches the state of the SDK at the time of our error call
    RequireEventMatch(nlohmann::json::parse(test.events[0].data), R"({
      "status": "error",
      "service": "mock-service",
      "date": "2023-11-14T22:13:20.000Z",
      "message": "This is an error",
      "ddtags": "service:mock-service,env:mock-env,sdk_version:1.2.3",
      "logger.name": "cool-logger",
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
      },
      "foo": 333,
      "bar": 444
    })");

    // And the second lines up with our warning call
    RequireEventMatch(nlohmann::json::parse(test.events[1].data), R"({
      "status": "warn",
      "service": "mock-service",
      "date": "2023-11-14T22:13:21.100Z",
      "message": "This is a warning",
      "ddtags": "service:mock-service,env:mock-env,sdk_version:1.2.3,foo:hello,bar:world,baz",
      "logger.name": "cool-logger",
      "logger.version": "1.2.3",
      "application_id": "0976f38a-ae45-4f7a-8436-0c98c227a7b3",
      "session_id": "ab45517b-40ae-4f90-8c8a-bff62c05a166",
      "usr": {
        "id": "u-123456"
      },
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
      },
      "foo": 11,
      "bar": 555,
      "baz": 666,
      "qux": 44,
      "arf": 55
    })");

    // And no diagnostic messages were emitted from our nominal AddTag calls
    REQUIRE(diagnostics.TotalSize() == 0);
  }
}
