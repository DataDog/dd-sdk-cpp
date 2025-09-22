// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/feature_scope.hpp"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <charconv>
#include <cinttypes>
#include <utility>

#include "core/context.hpp"
#include "core/feature_types/rum.hpp"
#include "datadog/core.hpp"
#include "support/threading.hpp"

using namespace datadog;
using namespace datadog::impl;

static const size_t MAX_EVENTS = 512;

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

struct FeatureState {
  std::array<uint64_t, MAX_EVENTS> events;
  std::atomic<size_t> num_events{0};

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
};

TEST_CASE("FeatureScope thread safety", "[unit][core][thread-safety]") {
  // Given a CoreContextProvider
  CoreContext initial_context(CoreConfig("client-token", "service", "env"));
  CoreContextProvider context_provider(initial_context);

  // And three independent features that will handle API calls from different threads
  FeatureState feature_a;
  FeatureState feature_b;
  FeatureState feature_c;

  // And a separate scope for each feature, initialized from the same context provider
  FeatureScope scope_a(context_provider, [&](Block event, Block event_metadata) {
    return feature_a.HandleEvent(event, event_metadata);
  });
  FeatureScope scope_b(context_provider, [&](Block event, Block event_metadata) {
    return feature_b.HandleEvent(event, event_metadata);
  });
  FeatureScope scope_c(context_provider, [&](Block event, Block event_metadata) {
    return feature_c.HandleEvent(event, event_metadata);
  });

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
  REQUIRE(sum > 100000);
  REQUIRE(sum < 160000);
}
