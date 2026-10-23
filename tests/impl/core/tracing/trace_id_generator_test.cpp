// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/trace_id_generator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

#include "mock/clock.hpp"

using namespace datadog::impl;

TEST_CASE("DefaultTraceIDGenerator", "[unit][tracing]") {
  SECTION("M generate timestamped IDs with varied random low bits") {
    constexpr std::size_t RUN_COUNT = 1000;
    constexpr std::size_t MINIMUM_UNIQUE_VALUES = RUN_COUNT * 95 / 100;

    auto clock = datadog::platform::Clock::Init();
    REQUIRE(clock != nullptr);

    DefaultTraceIDGenerator generator(*clock);
    const auto now_in_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        clock->Now().time_since_epoch()
    );
    const auto lower_time_bound = static_cast<uint32_t>(now_in_seconds.count());

    std::unordered_set<std::string> low_values;
    for (std::size_t iteration = 0; iteration < RUN_COUNT; ++iteration) {
      const TraceID id = generator.Generate();
      const uint64_t high = std::stoull(id.ToHexString().substr(0, 16), nullptr, 16);

      const auto seconds = static_cast<uint32_t>(high >> 32);
      REQUIRE(seconds >= lower_time_bound);
      REQUIRE(static_cast<uint32_t>(high) == 0);

      low_values.insert(id.ToLowDecimalString());
    }

    // The probability of this test failing, assuming a good quality, truly uniform
    // distribution is ~2.31e-760. So if it fails, there's probably a real bug.
    REQUIRE(low_values.size() >= MINIMUM_UNIQUE_VALUES);
  }

  SECTION("M high bits represent the current clock in seconds") {
    constexpr uint32_t KNOWN_SECONDS = 0x12345678;

    MockClock clock;
    clock.FreezeAt(
        datadog::Timestamp{datadog::Duration{std::chrono::seconds{KNOWN_SECONDS}}}
    );

    DefaultTraceIDGenerator generator(clock);
    const TraceID id = generator.Generate();
    const uint64_t high = std::stoull(id.ToHexString().substr(0, 16), nullptr, 16);

    REQUIRE(static_cast<uint32_t>(high >> 32) == KNOWN_SECONDS);
    REQUIRE(static_cast<uint32_t>(high) == 0);
  }
}
