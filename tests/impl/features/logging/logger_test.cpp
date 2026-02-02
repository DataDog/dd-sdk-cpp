// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/logging/logger.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "datadog/core.hpp"
#include "datadog/logging.hpp"

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/features/logging/logging.hpp"
#include "datadog/impl/platform/system_info.hpp"

#include "mock/clock.hpp"
#include "support/feature.hpp"

using namespace datadog;
using namespace datadog::impl;

static const CoreConfig CORE_CONFIG("mock-client-token", "mock-service", "mock-env");
static const platform::OsInfo OS_INFO{"mock-os", "2.3.4", "mock-build-number", "2"};
static const platform::DeviceInfo DEVICE_INFO{
    "desktop",
    "mock-device",
    "mock-model",
    "mock-brand",
    "x86_64",
    "en-US",
    "America/New_York"
};

TEST_CASE("Logger RUM enrichment", "[unit][logging]") {
  static const UUID uuid_9916 = *UUID::Parse("99163baf-48fe-458f-b777-eab1e4038342");
  static const UUID uuid_d927 = *UUID::Parse("d927ca19-c812-45bb-918c-977809e63c95");
  static const UUID uuid_e27c = *UUID::Parse("e27c9602-87b8-4722-b848-b58942212414");
  static const UUID uuid_cfa4 = *UUID::Parse("cfa45eee-3262-44a1-a9dc-afb5ddba7c85");

  auto has_uuid_value = [](const std::string& s, std::string_view name, UUID value) {
    std::string substr = "";
    substr += "\"";
    substr += name;
    substr += "\":\"";
    substr += value.ToString();
    substr += "\"";
    return s.find(substr) != std::string::npos;
  };

  auto has_no_value = [](const std::string& s, std::string_view name) {
    std::string substr = "";
    substr += "\"";
    substr += name;
    substr += "\":";
    return s.find(substr) == std::string::npos;
  };

  SECTION("M include RUM attributes W context contains valid RUM state") {
    // Given a valid Logging feature and a Logger with the default config
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    auto logging = std::make_shared<impl::Logging>(clock, "mock-service", "1.0.0");
    auto logger = logging->CreateLogger(LoggerConfig());

    // And a context that includes RUM session details
    CoreContext context(CORE_CONFIG, OS_INFO, DEVICE_INFO);
    context.rum = RumFeatureContext();
    context.rum->application_id = uuid_9916;
    context.rum->session_id = uuid_d927;
    context.rum->view_id = uuid_e27c;
    context.rum->action_id = uuid_cfa4;

    // When we emit a log event while the SDK is running
    FeatureTest test(context);
    test.Start(logging);
    logger->EmitLogEvent(LogLevel::Info, "hello");
    test.Stop(logging);

    // Then the resulting log event contains the requisite RUM attributes:
    // 'application_id', 'session_id', 'view.id', and 'user_action.id'
    REQUIRE(test.events.size() == 1);
    const CapturedEvent& event = test.events.back();
    REQUIRE(has_uuid_value(event.data, "application_id", uuid_9916));
    REQUIRE(has_uuid_value(event.data, "session_id", uuid_d927));
    REQUIRE(has_uuid_value(event.data, "view.id", uuid_e27c));
    REQUIRE(has_uuid_value(event.data, "user_action.id", uuid_cfa4));
  }

  SECTION("M omit individual RUM attributes W associated context value is UUID::Zero") {
    // Given a valid Logging feature and a Logger with the default config
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    auto logging = std::make_shared<impl::Logging>(clock, "mock-service", "1.0.0");
    auto logger = logging->CreateLogger(LoggerConfig());

    // And a context that includes a RUM application ID and session ID, but no view or
    // action ID
    CoreContext context(CORE_CONFIG, OS_INFO, DEVICE_INFO);
    context.rum = RumFeatureContext();
    context.rum->application_id = uuid_9916;
    context.rum->session_id = uuid_d927;

    // When we emit a log event while the SDK is running
    FeatureTest test(context);
    test.Start(logging);
    logger->EmitLogEvent(LogLevel::Info, "hello");
    test.Stop(logging);

    // Then the resulting log event contains 'application_id' and 'session_id', but not
    // 'view.id' or 'user_action.id'
    REQUIRE(test.events.size() == 1);
    const CapturedEvent& event = test.events.back();
    REQUIRE(has_uuid_value(event.data, "application_id", uuid_9916));
    REQUIRE(has_uuid_value(event.data, "session_id", uuid_d927));
    REQUIRE(has_no_value(event.data, "view.id"));
    REQUIRE(has_no_value(event.data, "user_action.id"));
  }

  SECTION("M preserve RUM attributes W user attributes have conflicting names") {
    // Given a valid Logging feature and a Logger with the default config
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    auto logging = std::make_shared<impl::Logging>(clock, "mock-service", "1.0.0");
    auto logger = logging->CreateLogger(LoggerConfig());

    // And a context that includes a RUM application ID, but no session
    CoreContext context(CORE_CONFIG, OS_INFO, DEVICE_INFO);
    context.rum = RumFeatureContext();
    context.rum->application_id = uuid_9916;

    // And user attributes named 'application_id' and 'session_id'
    logger->AddAttribute("application_id", Attribute::String("user-application-id"));
    logger->AddAttribute("session_id", Attribute::String("user-session-id"));

    // When we emit a log event while the SDK is running
    FeatureTest test(context);
    test.Start(logging);
    logger->EmitLogEvent(LogLevel::Info, "hello");
    test.Stop(logging);

    // Then the resulting log event contains:
    // - 'application_id' with our RUM context value, NOT the user-provided value
    // - No 'session_id' value, since no RUM session_id exists and RUM attribute names
    //   are reserved
    // - No 'view.id' or 'user_action.id' attributes, since nothing sets them
    REQUIRE(test.events.size() == 1);
    const CapturedEvent& event = test.events.back();
    REQUIRE(has_uuid_value(event.data, "application_id", uuid_9916));
    REQUIRE(event.data.find("\"user-application-id\"") == std::string::npos);
    REQUIRE(has_no_value(event.data, "session_id"));
    REQUIRE(has_no_value(event.data, "view.id"));
    REQUIRE(has_no_value(event.data, "user_action.id"));
  }

  SECTION("M not include RUM attributes W enrichment is explicitly disabled") {
    // Given a Logger configured with 'enrich_with_rum_context' disabled
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    auto logging = std::make_shared<impl::Logging>(clock, "mock-service", "1.0.0");
    auto logger = logging->CreateLogger(LoggerConfig().SetEnrichWithRumContext(false));

    // And a context that includes RUM session details
    CoreContext context(CORE_CONFIG, OS_INFO, DEVICE_INFO);
    context.rum = RumFeatureContext();
    context.rum->application_id = uuid_9916;
    context.rum->session_id = uuid_d927;
    context.rum->view_id = uuid_e27c;
    context.rum->action_id = uuid_cfa4;

    // When we emit a log event while the SDK is running
    FeatureTest test(context);
    test.Start(logging);
    logger->EmitLogEvent(LogLevel::Info, "hello");
    test.Stop(logging);

    // Then the resulting log event does not contain any RUM attributes
    REQUIRE(test.events.size() == 1);
    const CapturedEvent& event = test.events.back();
    REQUIRE(has_no_value(event.data, "application_id"));
    REQUIRE(has_no_value(event.data, "session_id"));
    REQUIRE(has_no_value(event.data, "view.id"));
    REQUIRE(has_no_value(event.data, "user_action.id"));
  }

  SECTION("M not include RUM attributes W RUM context is not present") {
    // Given a valid Logging feature and a Logger with the default config
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    auto logging = std::make_shared<impl::Logging>(clock, "mock-service", "1.0.0");
    auto logger = logging->CreateLogger(LoggerConfig());

    // And a context that has no RumFeatureContext
    CoreContext context(CORE_CONFIG, OS_INFO, DEVICE_INFO);

    // When we emit a log event while the SDK is running
    FeatureTest test(context);
    test.Start(logging);
    logger->EmitLogEvent(LogLevel::Info, "hello");
    test.Stop(logging);

    // Then the resulting log event does not contain any RUM attributes
    REQUIRE(test.events.size() == 1);
    const CapturedEvent& event = test.events.back();
    REQUIRE(has_no_value(event.data, "application_id"));
    REQUIRE(has_no_value(event.data, "session_id"));
    REQUIRE(has_no_value(event.data, "view.id"));
    REQUIRE(has_no_value(event.data, "user_action.id"));
  }
}
