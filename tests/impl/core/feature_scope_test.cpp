// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/feature_scope.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <charconv>
#include <cinttypes>
#include <utility>

#include "datadog/core.hpp"

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/context_thread.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/platform/system_info.hpp"

#include "support/diagnostics.hpp"
#include "support/threading.hpp"

using namespace datadog;
using namespace datadog::impl;

// Initial CoreContext values used in FeatureScope tests
static const platform::OsInfo MOCK_OS_INFO{
    "mock-os", "2.3.4", "mock-build-number", "2"
};
static const platform::DeviceInfo MOCK_DEVICE_INFO{
    "desktop",
    "mock-device",
    "mock-model",
    "mock-brand",
    "x86_64",
    "en-US",
    "America/New_York"
};
static const CoreContext MOCK_CONTEXT(
    CoreConfig("client-token", "service", "env"), MOCK_OS_INFO, MOCK_DEVICE_INFO
);

// FeatureScope tests rely on dummy features that produce events in the form of uint64
// values; we will buffer up to this number of events in a thread-safe array
static const size_t MAX_EVENTS = 512;

/**
 * Stand-in for a Feature that interacts with the FeatureScope in order to read
 * CoreContext, mutate CoreContext, and generate events.
 *
 * Provides an EventGeneratedFunc that buffers the event into a thread-safe array with a
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
 * Reads an arbitrary value from context, via FeatureScope, for use in events.
 */
static uint64_t ReadContextValue(FeatureScope& scope) {
  // Unpack an 8-byte uint from the first half of the RUM session_id
  CoreContext context = scope.GetContext();
  if (!context.rum) {
    return 0;
  }
  const RumFeatureContext& rum_ctx = *context.rum;
  const uint8_t* uuid_bytes = rum_ctx.session_id.bytes.data();
  const uint64_t* ptr = reinterpret_cast<const uint64_t*>(uuid_bytes);
  return *ptr;
}

/**
 * Writes an arbitrary value to context, via FeatureScope, to influence events
 * generated on any thread.
 */
static void WriteContextValue(FeatureScope& scope, uint64_t value) {
  // Pack an 8-byte uint into the first half of the RUM session_id
  UUID new_session_id = UUID::Zero;
  std::memcpy(new_session_id.bytes.data(), &value, sizeof(value));
  scope.UpdateContext([new_session_id](CoreContext& ctx) {
    if (!ctx.rum) {
      ctx.rum.emplace();
    }
    ctx.rum->session_id = new_session_id;
  });
}

TEST_CASE("FeatureScope thread safety", "[unit][core][thread-safety]") {
  // Given a CoreContextProvider
  CoreContextProvider context_provider(MOCK_CONTEXT);

  // And a DiagnosticLogger that will capture all emitted messages in a buffer
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger diagnostic_logger = diagnostics.CreateTestLogger();

  // And a fully functional context thread that reads functions from a queue and
  // executes them serially in the background
  Queue<std::function<void()>> context_queue;
  std::thread context_thread{
      ContextThreadMain,
      std::ref(diagnostic_logger),
      std::ref(context_queue),
      std::ref(context_provider)
  };
  auto stop_context_thread = [&]() {
    context_queue.Stop();
    context_thread.join();
  };

  // And three independent features that will handle API calls from different threads
  FeatureState feature_a;
  FeatureState feature_b;
  FeatureState feature_c;

  // And a separate scope for each feature, initialized from the same context provider
  FeatureScope scope_a = feature_a.CreateScope(context_provider, context_queue);
  FeatureScope scope_b = feature_b.CreateScope(context_provider, context_queue);
  FeatureScope scope_c = feature_c.CreateScope(context_provider, context_queue);

  // When we run three threads, each of which sporadically reads from or writes to a
  // shared context value, then produces an event based on that value, 512 times each
  auto threads = RunParallel(3, [&](size_t thread_id) {
    FeatureScope& scope =
        thread_id == 0 ? scope_a : (thread_id == 1 ? scope_b : scope_c);

    for (uint64_t i = 0; i < MAX_EVENTS; i++) {
      // Either write `i` to context or read the most recent value from context
      uint64_t value = i;
      const uint64_t x = (thread_id * thread_id) + i;
      if (x % 3 == 0) {
        WriteContextValue(scope, i);
      } else {
        value = ReadContextValue(scope);
      }

      // Produce an event whose payload is just a string-encoded version of our value
      char buf[20];
      auto res = std::to_chars(buf, buf + sizeof(buf), value);
      REQUIRE(res.ec == std::errc{});
      scope.WriteEvent(Block(buf, res.ptr - buf), {});
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
