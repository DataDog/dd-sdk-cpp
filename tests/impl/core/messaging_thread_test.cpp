// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/messaging_thread.hpp"

#include <cstring>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/message_bus.hpp"

#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

// Tests run MessagingThreadMain synchronously by stopping the queue before calling the
// function, avoiding any background-thread synchronization.

TEST_CASE("MessagingThreadMain", "[unit][core]") {
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  SECTION("M log debug messages on start and stop W run on empty stopped bus") {
    // Given an empty bus with no handlers and a stopped queue
    MessageBus bus({});
    bus.Stop();

    // When we run the messaging thread synchronously
    MessagingThreadMain(logger, bus);

    // Then exactly the start and stop messages are emitted — nothing else
    REQUIRE(diagnostics.debug.size() == 2);
    REQUIRE(diagnostics.debug[0] == "Messaging thread starting");
    REQUIRE(diagnostics.debug[1] == "Messaging thread finished");
    REQUIRE(diagnostics.TotalSize() == 2);
  }

  SECTION("M deliver enqueued messages to all handlers in order") {
    // Given a bus with two handlers that each record which messages they receive
    std::vector<int> received_a;
    std::vector<int> received_b;

    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([&](const FeatureMessage& msg) {
      const auto& ccm = std::get<ContextChangedMessage>(msg);
      int value = 0;
      std::memcpy(&value, ccm.context.rum->session_id.bytes.data(), sizeof(value));
      received_a.push_back(value);
    });
    handlers.push_back([&](const FeatureMessage& msg) {
      const auto& ccm = std::get<ContextChangedMessage>(msg);
      int value = 0;
      std::memcpy(&value, ccm.context.rum->session_id.bytes.data(), sizeof(value));
      received_b.push_back(value);
    });

    MessageBus bus(std::move(handlers));

    // Enqueue two messages, each carrying a distinct int sentinel
    for (int i : {1, 2}) {
      CoreContext ctx = MOCK_CONTEXT;
      ctx.rum.emplace();
      std::memcpy(ctx.rum->session_id.bytes.data(), &i, sizeof(i));
      bus.Send(ContextChangedMessage{std::move(ctx)});
    }
    bus.Stop();

    // When we run the messaging thread synchronously
    MessagingThreadMain(logger, bus);

    // Then both handlers received both messages in order, with no extra diagnostics
    REQUIRE(received_a == std::vector<int>{1, 2});
    REQUIRE(received_b == std::vector<int>{1, 2});
    REQUIRE(diagnostics.debug.size() == 2);
    REQUIRE(diagnostics.debug.size() == diagnostics.TotalSize());
  }
}
