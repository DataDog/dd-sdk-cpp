// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/message_bus.hpp"

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/messaging_thread.hpp"

#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

/**
 * Constructs a ContextChangedMessage carrying a CoreContext whose RUM session_id
 * first-half bytes encode the given sentinel value, for use as a unique identity marker
 * in delivery assertions.
 */
static ContextChangedMessage MakeMessage(uint64_t sentinel) {
  CoreContext ctx = MOCK_CONTEXT;
  ctx.rum.emplace();
  std::memcpy(ctx.rum->session_id.bytes.data(), &sentinel, sizeof(sentinel));
  return ContextChangedMessage{std::move(ctx)};
}

/**
 * Extracts the sentinel value packed into the first half of the RUM session_id by
 * MakeMessage.
 */
static uint64_t ReadSentinel(const ContextChangedMessage& msg) {
  uint64_t value = 0;
  if (msg.context.rum) {
    std::memcpy(&value, msg.context.rum->session_id.bytes.data(), sizeof(value));
  }
  return value;
}

TEST_CASE("MessageBus", "[unit][core]") {
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  SECTION("M deliver message to single handler W one handler registered") {
    // Given a bus with one handler that records delivered sentinel values
    std::vector<uint64_t> received;
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([&](const FeatureMessage& msg) {
      received.push_back(ReadSentinel(std::get<ContextChangedMessage>(msg)));
    });

    MessageBus bus(std::move(handlers));
    std::thread t(MessagingThreadMain, std::ref(logger), std::ref(bus));

    // When we send a message with a known sentinel and stop the bus
    REQUIRE(bus.Send(MakeMessage(0xbeef)));
    bus._queue.Stop();
    t.join();

    // Then the handler received exactly that message
    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == 0xbeef);
  }

  SECTION("M broadcast to all handlers W multiple handlers registered") {
    // Given a bus with three handlers, each recording which sentinels it received
    std::vector<uint64_t> received_a, received_b, received_c;
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([&](const FeatureMessage& msg) {
      received_a.push_back(ReadSentinel(std::get<ContextChangedMessage>(msg)));
    });
    handlers.push_back([&](const FeatureMessage& msg) {
      received_b.push_back(ReadSentinel(std::get<ContextChangedMessage>(msg)));
    });
    handlers.push_back([&](const FeatureMessage& msg) {
      received_c.push_back(ReadSentinel(std::get<ContextChangedMessage>(msg)));
    });

    MessageBus bus(std::move(handlers));
    std::thread t(MessagingThreadMain, std::ref(logger), std::ref(bus));

    // When we send two messages and stop the bus
    REQUIRE(bus.Send(MakeMessage(1)));
    REQUIRE(bus.Send(MakeMessage(2)));
    bus._queue.Stop();
    t.join();

    // Then every handler received both messages, in order
    const std::vector<uint64_t> expected{1, 2};
    REQUIRE(received_a == expected);
    REQUIRE(received_b == expected);
    REQUIRE(received_c == expected);
  }

  SECTION("M drain all queued messages before Stop() returns") {
    // Given a bus with a handler that counts deliveries
    std::atomic<int> count{0};
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([&](const FeatureMessage&) { count.fetch_add(1); });

    MessageBus bus(std::move(handlers));
    std::thread t(MessagingThreadMain, std::ref(logger), std::ref(bus));

    // When we enqueue several messages and then stop
    const int num_messages = 50;
    for (int i = 0; i < num_messages; i++) {
      bus.Send(MakeMessage(static_cast<uint64_t>(i)));
    }
    bus._queue.Stop();
    t.join();

    // Then all messages were delivered before the thread exited
    REQUIRE(count.load() == num_messages);
  }

  SECTION("M return false from Send() W bus is stopped") {
    // Given a bus with no handlers
    MessageBus bus({});
    std::thread t(MessagingThreadMain, std::ref(logger), std::ref(bus));

    // When the bus is stopped
    bus._queue.Stop();
    t.join();

    // Then Send() returns false and the message is dropped
    REQUIRE_FALSE(bus.Send(MakeMessage(0xdead)));
  }

  SECTION("M not crash or block other handlers W one handler is a no-op") {
    // Given a bus whose first handler does nothing (simulating a dead feature) and
    // whose second handler records delivered sentinels
    std::vector<uint64_t> received;
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([](const FeatureMessage&) {
      // Deliberately empty — simulates a handler whose owning feature was torn down but
      // whose lambda was not removed from the bus.
    });
    handlers.push_back([&](const FeatureMessage& msg) {
      received.push_back(ReadSentinel(std::get<ContextChangedMessage>(msg)));
    });

    MessageBus bus(std::move(handlers));
    std::thread t(MessagingThreadMain, std::ref(logger), std::ref(bus));

    REQUIRE(bus.Send(MakeMessage(0x42)));
    bus._queue.Stop();
    t.join();

    // The active handler still received the message
    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == 0x42);
  }
}
