// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/message_bus.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "datadog/impl/core/executor.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/feature_id.hpp"
#include "datadog/impl/core/feature_message.hpp"

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

/**
 * Minimal Feature subclass used to test MakeMessageHandler() wiring. Writes delivered
 * ContextChangedMessage values into an external vector whose lifetime exceeds the
 * feature's, which allows test assertions even when the feature is destroyed early.
 */
struct MessageBusTestFeature final : public Feature {
  std::vector<ContextChangedMessage>& received;

  explicit MessageBusTestFeature(std::vector<ContextChangedMessage>& out)
      : received(out) {}

  FeatureId GetId() const override { return CreateFeatureId("MBUS"); }
  std::string_view GetName() const override { return "message_bus_test"; }
  std::optional<Report> UploadThread_PrepareReport(
      BatchReader&, HttpRequestBuilder&
  ) override {
    return std::nullopt;
  }

  std::optional<std::function<void(const FeatureMessage&)>>
  MakeMessageHandler() override {
    // Capture both the weak_ptr (for lifetime gating) and a direct reference to the
    // output vector (which outlives the Feature in tests). Since `received` is a
    // reference member, `&out` in the lambda binds to the external vector, not to
    // this object, so the reference remains valid after the Feature is destroyed.
    auto weak = weak_from_this();
    std::vector<ContextChangedMessage>& out = received;
    return [weak, &out](const FeatureMessage& msg) {
      if (!weak.lock()) {
        return;
      }
      out.push_back(std::get<ContextChangedMessage>(msg));
    };
  }
};

TEST_CASE("MessageBus - integration", "[unit][core]") {
  // Declared first, so that it outlives every bus created below
  Executor executor;

  SECTION(
      "M deliver ContextChangedMessage W MakeMessageHandler() wired through "
      "CoreContextProvider::Update()"
  ) {
    // Given a feature registered via MakeMessageHandler()
    std::vector<ContextChangedMessage> received;
    auto feature = std::make_shared<MessageBusTestFeature>(received);
    auto handler = feature->MakeMessageHandler();
    REQUIRE(handler.has_value());

    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back(std::move(*handler));

    MessageBus bus(executor, std::move(handlers));
    bus.Resume();

    CoreContextProvider context_provider(MOCK_CONTEXT);
    context_provider.SetMessageBus(&bus);

    // When Update() is called
    context_provider.Update([](CoreContext& ctx) { ctx.rum.emplace(); });

    // Then the handler receives the message
    bus.Stop();
    context_provider.SetMessageBus(nullptr);

    REQUIRE(received.size() == 1);
  }

  SECTION("M deliver correct context snapshot W context mutated in Update()") {
    // Given a bus with a plain lambda handler
    std::vector<ContextChangedMessage> received;
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([&](const FeatureMessage& msg) {
      received.push_back(std::get<ContextChangedMessage>(msg));
    });

    MessageBus bus(executor, std::move(handlers));
    bus.Resume();

    CoreContextProvider context_provider(MOCK_CONTEXT);
    context_provider.SetMessageBus(&bus);

    // When Update() writes a known sentinel value into the RUM session_id
    constexpr uint64_t sentinel = 0xc0ffee;
    context_provider.Update([=](CoreContext& ctx) {
      ctx.rum.emplace();
      std::memcpy(ctx.rum->session_id.bytes.data(), &sentinel, sizeof(sentinel));
    });

    bus.Stop();
    context_provider.SetMessageBus(nullptr);

    // Then the delivered snapshot carries that exact value
    REQUIRE(received.size() == 1);
    REQUIRE(received[0].context.rum.has_value());
    uint64_t got = 0;
    std::memcpy(&got, received[0].context.rum->session_id.bytes.data(), sizeof(got));
    REQUIRE(got == sentinel);
  }

  SECTION(
      "M skip delivery gracefully W feature is destroyed before message is handled"
  ) {
    // Given a feature whose handler captures weak_from_this(), and a second handler
    // that counts deliveries to confirm the bus continues to function
    std::vector<ContextChangedMessage> received;
    auto feature = std::make_shared<MessageBusTestFeature>(received);
    auto handler = feature->MakeMessageHandler();
    REQUIRE(handler.has_value());

    std::atomic<int> other_count{0};
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back(std::move(*handler));
    handlers.push_back([&](const FeatureMessage&) { other_count.fetch_add(1); });

    // Enqueue a message while the feature is still alive, then destroy it before the
    // message is delivered. The bus starts suspended, so delivery only happens once
    // Stop() drains it, which guarantees the feature is gone when the handler runs.
    MessageBus bus(executor, std::move(handlers));
    REQUIRE(bus.Send(MakeMessage(0x1)));

    feature.reset();  // weak_from_this() in handler now expires

    bus.Stop();

    // The TestFeature handler silently dropped the message; the other handler ran
    REQUIRE(received.empty());
    REQUIRE(other_count.load() == 1);
  }

  SECTION(
      "M deliver all messages enqueued before Stop() W shutdown via "
      "CoreContextProvider::Update()"
  ) {
    // Given a bus wired to a context provider with a counting handler
    std::atomic<int> delivered{0};
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([&](const FeatureMessage&) { delivered.fetch_add(1); });

    MessageBus bus(executor, std::move(handlers));
    bus.Resume();

    CoreContextProvider context_provider(MOCK_CONTEXT);
    context_provider.SetMessageBus(&bus);

    // When multiple Update() calls are made before the bus is stopped
    const int num_updates = 20;
    for (int i = 0; i < num_updates; i++) {
      context_provider.Update([](CoreContext& ctx) { ctx.rum.emplace(); });
    }

    bus.Stop();
    context_provider.SetMessageBus(nullptr);

    // Then all messages were delivered before Stop() returned
    REQUIRE(delivered.load() == num_updates);
  }
}

TEST_CASE("MessageBus", "[unit][core]") {
  // Declared first, so that it outlives every bus created below
  Executor executor;

  SECTION("M deliver message to single handler W one handler registered") {
    // Given a bus with one handler that records delivered sentinel values
    std::vector<uint64_t> received;
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([&](const FeatureMessage& msg) {
      received.push_back(ReadSentinel(std::get<ContextChangedMessage>(msg)));
    });

    MessageBus bus(executor, std::move(handlers));
    bus.Resume();

    // When we send a message with a known sentinel and stop the bus
    REQUIRE(bus.Send(MakeMessage(0xbeef)));
    bus.Stop();

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

    MessageBus bus(executor, std::move(handlers));
    bus.Resume();

    // When we send two messages and stop the bus
    REQUIRE(bus.Send(MakeMessage(1)));
    REQUIRE(bus.Send(MakeMessage(2)));
    bus.Stop();

    // Then every handler received both messages, in order
    const std::vector<uint64_t> expected{1, 2};
    REQUIRE(received_a == expected);
    REQUIRE(received_b == expected);
    REQUIRE(received_c == expected);
  }

  SECTION("M hold messages until resumed W messages sent before Resume()") {
    // Given a bus that hasn't been resumed yet, with a handler that counts deliveries
    std::atomic<int> count{0};
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([&](const FeatureMessage&) { count.fetch_add(1); });
    MessageBus bus(executor, std::move(handlers));

    // When we send a message and give the executor ample time to run it
    REQUIRE(bus.Send(MakeMessage(1)));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const int count_before_resume = count.load();

    // And then resume and stop the bus
    bus.Resume();
    bus.Stop();

    // Then the message was only delivered after Resume()
    REQUIRE(count_before_resume == 0);
    REQUIRE(count.load() == 1);
  }

  SECTION("M drain all queued messages before Stop() returns") {
    // Given a bus with a handler that counts deliveries
    std::atomic<int> count{0};
    std::vector<std::function<void(const FeatureMessage&)>> handlers;
    handlers.push_back([&](const FeatureMessage&) { count.fetch_add(1); });

    MessageBus bus(executor, std::move(handlers));
    bus.Resume();

    // When we enqueue several messages and then stop
    const int num_messages = 50;
    for (int i = 0; i < num_messages; i++) {
      bus.Send(MakeMessage(static_cast<uint64_t>(i)));
    }
    bus.Stop();

    // Then all messages were delivered before Stop() returned
    REQUIRE(count.load() == num_messages);
  }

  SECTION("M return false from Send() W bus is stopped") {
    // Given a bus with no handlers
    MessageBus bus(executor, {});
    bus.Resume();

    // When the bus is stopped
    bus.Stop();

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

    MessageBus bus(executor, std::move(handlers));
    bus.Resume();

    REQUIRE(bus.Send(MakeMessage(0x42)));
    bus.Stop();

    // The active handler still received the message
    REQUIRE(received.size() == 1);
    REQUIRE(received[0] == 0x42);
  }
}
