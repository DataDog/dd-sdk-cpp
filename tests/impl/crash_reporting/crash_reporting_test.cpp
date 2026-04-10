// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/crash_reporting.hpp"

#include <memory>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/storage/path.hpp"

#include "mock/filesystem.hpp"
#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/crash_handler.hpp"
#include "support/feature.hpp"

using namespace datadog::impl;

static const UUID APPLICATION_ID = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
static const UUID SESSION_ID = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
static const UUID VIEW_ID = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
static const CoreConfig CORE_CONFIG("mock-client-token", "mock-service", "mock-env");

TEST_CASE("CrashReporting message handling", "[unit][crash_reporting]") {
  // Given a mock CoreContext value that will be copied into ContextChangedMessage
  CoreContext context{CORE_CONFIG, MOCK_OS_INFO, MOCK_DEVICE_INFO};

  // And some required ICrashHandler dependencies that are not yet exercised in tests
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

  SECTION(
      "M convey RUM context to handler W ContextChangedMessage supplies new RUM context"
  ) {
    // When the feature handles a ContextChangedMessage that provides new RUM context
    context.rum.emplace(
        RumFeatureContext{APPLICATION_ID, SESSION_ID, VIEW_ID, UUID::Zero}
    );
    message_handler_func(ContextChangedMessage{context});

    // Then our handler has been notified of the latest RUM context values
    REQUIRE(handler.num_set_rum_context_calls == 1);
    REQUIRE(handler.last_rum_ctx.has_value());
    REQUIRE(handler.last_rum_ctx->application_id == APPLICATION_ID);
    REQUIRE(handler.last_rum_ctx->session_id == SESSION_ID);
    REQUIRE(handler.last_rum_ctx->view_id == VIEW_ID);
    REQUIRE(handler.last_rum_ctx->action_id == UUID::Zero);

    SECTION(
        "M not convey RUM context to handler W ContextChangedMessage carries no "
        "meaningful changes to RUM context"
    ) {
      // When another message arrives with identical RumFeatureContext values
      context.http.reset();  // Change CoreContext, but not its rum member
      message_handler_func(ContextChangedMessage{context});

      // Then no ICrashHandler::SetRumContext call was made
      REQUIRE(handler.num_set_rum_context_calls == 1);
      REQUIRE(handler.last_rum_ctx.has_value());
      REQUIRE(handler.last_rum_ctx->application_id == APPLICATION_ID);
      REQUIRE(handler.last_rum_ctx->session_id == SESSION_ID);
      REQUIRE(handler.last_rum_ctx->view_id == VIEW_ID);
      REQUIRE(handler.last_rum_ctx->action_id == UUID::Zero);
    }

    SECTION(
        "M convey updated RUM context to handler W ContextChangedMessage carries any "
        "change to RUM context"
    ) {
      // When another message arrives with different RumFeatureContext values
      const UUID new_view_id = *UUID::Parse("c1488c09-e763-41e6-8f53-4f40d6916c31");
      const UUID new_action_id = *UUID::Parse("10cb6a8f-852f-46f6-8ce4-6d9265e77dac");
      context.rum->view_id = new_view_id;
      context.rum->action_id = new_action_id;
      message_handler_func(ContextChangedMessage{context});

      // Then a second ICrashHandler::SetRumContext call was made
      REQUIRE(handler.num_set_rum_context_calls == 2);
      REQUIRE(handler.last_rum_ctx.has_value());
      REQUIRE(handler.last_rum_ctx->application_id == APPLICATION_ID);
      REQUIRE(handler.last_rum_ctx->session_id == SESSION_ID);
      REQUIRE(handler.last_rum_ctx->view_id == new_view_id);
      REQUIRE(handler.last_rum_ctx->action_id == new_action_id);
    }
  }

  SECTION("M safely do nothing W message arrives after feature is destroyed") {
    // When the last std::shared_ptr<CrashReporting> is destroyed, causing the feature
    // implementation to be destroyed
    crash_reporting.reset();

    // And a lingering message-handler callback receives a message that would ordinarily
    // result in a state change within crash reporting
    REQUIRE(message_handler_func);
    context.rum.emplace(
        RumFeatureContext{APPLICATION_ID, SESSION_ID, UUID::Zero, UUID::Zero}
    );
    message_handler_func(ContextChangedMessage{context});

    // Then nothing happens: the weak_ptr check in the handler callback causes the
    // message to be dropped, preventing use-after-free
    REQUIRE(handler.num_set_rum_context_calls == 0);
    REQUIRE(!handler.last_rum_ctx.has_value());
  }
}
