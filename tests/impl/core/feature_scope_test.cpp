// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/feature_scope.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <cinttypes>
#include <utility>

#include "datadog/core.hpp"

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/context_thread.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/platform/system_info.hpp"

#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/diagnostics.hpp"
#include "support/threading.hpp"

using namespace datadog;
using namespace datadog::impl;

// FeatureScope tests rely on dummy features that produce events in the form of uint64
// values; we will buffer up to this number of events in a thread-safe array
static const size_t MAX_EVENTS = 512;

/**
 * Stand-in for a Feature that interacts with the FeatureScope in order to read
 * CoreContext, mutate CoreContext, and generate events.
 *
 * Provides an EventWriter that buffers the event into a thread-safe array with a
 * fixed capacity.
 */
struct FeatureState {
  std::array<uint64_t, MAX_EVENTS> events;
  std::atomic<size_t> num_events{0};

  /**
   * Parses `event` as a string-formatted uint64_t, then atomically stores it in the
   * next available slot within the events array. If `events` is at capacity, a test
   * assert will be triggered.
   */
  bool HandleEvent(Block event, Block event_metadata) {
    // Metadata is unused in these tests
    REQUIRE(event_metadata.empty());

    // Parse the event payload as a non-null-terminated ASCII-encoded int
    uint64_t value;
    auto result = std::from_chars(event.data(), event.data() + event.size(), value);
    REQUIRE(result.ec == std::errc{});

    // Atomically reserve an array slot and write the value to it
    const size_t i = num_events.fetch_add(1, std::memory_order_relaxed);
    REQUIRE(i >= 0);
    REQUIRE(i < MAX_EVENTS);
    events[i] = value;
    return true;
  }

  /**
   * Creates a FeatureScope that can be used to simulate the interactions of a `Feature`
   * implementation with the Core.
   */
  FeatureScope CreateScope(
      CoreContextProvider& context_provider, Queue<std::function<void()>>& context_queue
  ) {
    return FeatureScope::Create(
        context_provider,
        [this](Block event, Block event_metadata) {
          return this->HandleEvent(event, event_metadata);
        },
        DiagnosticLogger{},
        context_queue
    );
  }
};

/**
 * Writes an arbitrary 64-bit unsigned int into the CoreContext, such that it can be
 * retrieved by ReadContextValue.
 */
static void WriteContextValue(CoreContext& ctx, uint64_t value) {
  // Pack an 8-byte uint into the first half of the RUM session_id
  UUID new_session_id = UUID::Zero;
  std::memcpy(new_session_id.bytes.data(), &value, sizeof(value));
  if (!ctx.rum) {
    ctx.rum.emplace();
  }
  ctx.rum->session_id = new_session_id;
}

/**
 * Serializes `value` as a string and passes it to the given EventWriter function,
 * thereby producing an event whose binary value is a string-encoded uint64_t.
 */
static bool GenerateUInt64Event(const EventWriter& event_writer, uint64_t value) {
  char buf[20];
  auto res = std::to_chars(buf, buf + sizeof(buf), value);
  REQUIRE(res.ec == std::errc{});
  return event_writer(Block(buf, res.ptr - buf), {});
}

/**
 * Returns the arbitrary value that was stored in the provided CoreContext value, or 0
 * if no such value was stored.
 */
static uint64_t ReadContextValue(const CoreContext& ctx) {
  // Unpack an 8-byte uint from the first half of the RUM session_id
  if (!ctx.rum) {
    return 0;
  }
  const RumFeatureContext& rum_ctx = *ctx.rum;
  const uint8_t* uuid_bytes = rum_ctx.session_id.bytes.data();
  const uint64_t* ptr = reinterpret_cast<const uint64_t*>(uuid_bytes);
  return *ptr;
}

TEST_CASE("FeatureScope", "[unit][core]") {
  // Given a CoreContextProvider
  CoreContextProvider context_provider(MOCK_CONTEXT);

  // And a DiagnosticLogger that will capture all emitted messages in a buffer
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger diagnostic_logger = diagnostics.CreateTestLogger();

  // And a fully functional context thread that reads functions from a queue and
  // executes them serially in the background
  Queue<std::function<void()>> context_queue;
  std::thread context_thread{
      ContextThreadMain, std::ref(diagnostic_logger), std::ref(context_queue)
  };
  auto stop_context_thread = [&]() {
    context_queue.Stop();
    context_thread.join();
  };

  SECTION("M run function on context thread W enqueued via ExecuteOnContextThread") {
    // Given a single feature with a FeatureScope
    FeatureState feature;
    FeatureScope scope = feature.CreateScope(context_provider, context_queue);

    // When we offload a function to be run on the context thread, and then join on the
    // context thread
    int num_executions = 0;
    auto main_thread_id = std::this_thread::get_id();
    scope.ExecuteOnContextThread([&](const CoreContext&, EventWriter) {
      num_executions++;

      // Then our function is executed on a background thread
      auto context_thread_id = std::this_thread::get_id();
      REQUIRE(context_thread_id != main_thread_id);
    });
    stop_context_thread();

    // And our function did indeed execute
    REQUIRE(num_executions == 1);
  }

  SECTION(
      "M run CoreContext-mutation function on context thread W enqueud via "
      "UpdateContext"
  ) {
    // Given a single feature with a FeatureScope
    FeatureState feature;
    FeatureScope scope = feature.CreateScope(context_provider, context_queue);

    // When we enqueue a function that writes an arbitrary value to CoreContext
    int num_updates = 0;
    scope.UpdateContext([&](CoreContext& ctx) {
      WriteContextValue(ctx, 0xbeef);
      num_updates++;
    });

    // And then we enqueue a read-only function that will run thereafter
    int num_executions = 0;
    scope.ExecuteOnContextThread([&](const CoreContext& ctx, EventWriter) {
      // Then the CoreContext snapshot passed to our second function reflects the
      // modifications made by the first
      REQUIRE(ReadContextValue(ctx) == 0xbeef);
      num_executions++;
    });

    // And both functions actually ran
    stop_context_thread();
    REQUIRE(num_updates == 1);
    REQUIRE(num_executions == 1);
  }

  SECTION(
      "M provide functions with a consistent view of CoreContext W mutations and reads "
      "are enqueued in a consistent order"
  ) {
    // Given two mock features, one that writes a value to CoreContext and another that
    // reads from CoreContext and produces events that include that value
    FeatureState context_mutator;
    FeatureState context_consumer;

    // And the FeatureScope interfaces that would be used by each feature
    FeatureScope mutate_scope =
        context_mutator.CreateScope(context_provider, context_queue);
    FeatureScope consume_scope =
        context_consumer.CreateScope(context_provider, context_queue);

    uint64_t generation = 0;
    auto mutate = [&](CoreContext& ctx) {
      generation++;
      WriteContextValue(ctx, generation);
    };
    auto consume = [](const CoreContext& ctx, EventWriter event_writer) {
      const uint64_t value = ReadContextValue(ctx);
      char buf[20];
      auto res = std::to_chars(buf, buf + sizeof(buf), value);
      REQUIRE(res.ec == std::errc{});
      event_writer(Block(buf, res.ptr - buf), {});
    };

    mutate_scope.UpdateContext(mutate);             // Stores generation 1 in context
    consume_scope.ExecuteOnContextThread(consume);  // Produces '1'
    consume_scope.ExecuteOnContextThread(consume);  // Produces '1'
    mutate_scope.UpdateContext(mutate);             // Stores generation 2 in context
    mutate_scope.UpdateContext(mutate);             // Stores generation 3 in context
    consume_scope.ExecuteOnContextThread(consume);  // Produces '3'
    stop_context_thread();

    REQUIRE(context_mutator.num_events == 0);
    REQUIRE(context_consumer.num_events == 3);
    REQUIRE(context_consumer.events[0] == 1);
    REQUIRE(context_consumer.events[1] == 1);
    REQUIRE(context_consumer.events[2] == 3);
  }

  SECTION("M handle concurrently-enqueued operations") {
    // Given three independent features that will handle API calls from different
    // threads
    FeatureState feature_a;
    FeatureState feature_b;
    FeatureState feature_c;

    // And a separate scope for each feature, initialized from the same context provider
    FeatureScope scope_a = feature_a.CreateScope(context_provider, context_queue);
    FeatureScope scope_b = feature_b.CreateScope(context_provider, context_queue);
    FeatureScope scope_c = feature_c.CreateScope(context_provider, context_queue);

    // When we run three threads, each of which enqueues a different set of
    // context-thread functions on behalf of a different feature
    auto threads = RunParallel(3, [&](size_t thread_id) {
      // Select one of our three scopes based on our thread ID
      FeatureScope& scope =
          thread_id == 0 ? scope_a : (thread_id == 1 ? scope_b : scope_c);

      // Enqueue exactly MAX_EVENTS functions to run on the context thread, while also
      // sometimes enqueuing a mutation of the CoreContext value that all threads are
      // reading from
      for (uint64_t i = 0; i < MAX_EVENTS; i++) {
        // Every so often, write our current loop counter `i` into the CoreContext
        const bool should_mutate_context = ((thread_id * thread_id) + i) % 3 == 0;
        if (should_mutate_context) {
          scope.UpdateContext([i](CoreContext& ctx) { WriteContextValue(ctx, i); });
        }

        // On each iteration, enqueue a function that will run on the context thread and
        // produce an event with the latest value read from CoreContext
        scope.ExecuteOnContextThread([](const CoreContext& ctx,
                                        EventWriter event_writer) {
          // Produce an event whose payload is just a string version of our value
          GenerateUInt64Event(event_writer, ReadContextValue(ctx));
        });
      }
    });

    // And we join on all threads
    for (auto& thread : threads) {
      thread.join();
    }
    stop_context_thread();

    // Then we should have populated 512 events for each feature
    REQUIRE(feature_a.num_events.load(std::memory_order_relaxed) == MAX_EVENTS);
    REQUIRE(feature_b.num_events.load(std::memory_order_relaxed) == MAX_EVENTS);
    REQUIRE(feature_c.num_events.load(std::memory_order_relaxed) == MAX_EVENTS);

    // And the range of values should be [0..512]
    uint64_t sum = 0;
    for (uint64_t value : feature_a.events) {
      REQUIRE(value >= 0);
      REQUIRE(value <= MAX_EVENTS);
      sum += value;
    }

    // And the sum of all values in a single array should be roughly `sum(range(512))`,
    // i.e. 130816, give or take a sizable margin of error
    REQUIRE(sum > 90000);
    REQUIRE(sum < 170000);
  }

  SECTION("M drain queue and execute all enqueued functions W stopped") {
    // TODO(RUM-15042): Currently the SDK flushes the context queue on shutdown; if this
    // changes, this test will need to change or else use a test-only flush method

    // Given a single feature with a FeatureScope
    FeatureState feature;
    FeatureScope scope = feature.CreateScope(context_provider, context_queue);

    // And a promise that will block the context thread until we explicitly resolve it
    std::promise<void> gate;
    std::future<void> gate_signal = gate.get_future();
    scope.ExecuteOnContextThread([&](const CoreContext&, EventWriter) {
      gate_signal.wait();
    });

    // When we enqueue a bunch of functions to run on the context thread
    for (size_t i = 0; i < MAX_EVENTS; i++) {
      scope.ExecuteOnContextThread([i](const CoreContext&, EventWriter event_writer) {
        GenerateUInt64Event(event_writer, i);
      });
    }

    // And then we signal that the context queue should stop accepting new events due to
    // shutdown
    context_queue.Stop();

    // And then we unblock queue processing on the context thread
    gate.set_value();

    // And then we join on the context thread
    context_thread.join();

    // Then we end up with events produced by all functions
    REQUIRE(feature.num_events == MAX_EVENTS);
    for (size_t i = 0; i < MAX_EVENTS; i++) {
      REQUIRE(feature.events[i] == i);
    }
  }
}
