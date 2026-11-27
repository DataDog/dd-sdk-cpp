// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/platform/cpu_info_provider.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <optional>

#include "datadog/timestamp.hpp"

using namespace datadog;

// Smoke tests against the real OS: exact percentages are not asserted, to avoid flakes

TEST_CASE("CpuInfoProvider", "[unit][platform-cpu-info-provider]") {
  const platform::CpuInfoProvider cpu;

  SECTION("M return the CPU time W it is read") {
    const std::optional<Duration> cpu_time = cpu.CpuTime();
    REQUIRE(cpu_time.has_value());
    CHECK(*cpu_time >= Duration::zero());
  }

  SECTION("M return a positive CPU count W it is read") {
    const std::optional<uint32_t> count = cpu.LogicalCpuCount();
    REQUIRE(count.has_value());
    CHECK(*count > 0);
  }

  SECTION("M never decrease the CPU time W it is read repeatedly") {
    std::optional<Duration> previous = cpu.CpuTime();
    REQUIRE(previous.has_value());
    for (int i = 0; i < 1000; i++) {
      const std::optional<Duration> current = cpu.CpuTime();
      REQUIRE(current.has_value());
      REQUIRE(*current >= *previous);
      previous = current;
    }
  }

  SECTION("M increase the CPU time W the process is busy") {
    const std::optional<Duration> before = cpu.CpuTime();
    REQUIRE(before.has_value());

    // Spin until the CPU time moves; on Windows it moves in scheduler ticks (~15.6 ms)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::optional<Duration> after = before;
    volatile uint64_t sink = 0;
    while (after.has_value() && *after <= *before &&
           std::chrono::steady_clock::now() < deadline) {
      for (int i = 0; i < 100'000; i++) {
        sink = sink + static_cast<uint64_t>(i);
      }
      after = cpu.CpuTime();
    }
    REQUIRE(after.has_value());
    CHECK(*after > *before);
  }
}
