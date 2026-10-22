// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profiler_ownership.hpp"

#include "support/catch.hpp"

using namespace datadog::impl;

TEST_CASE("ProfilerOwnership", "[unit][profiling]") {
  SECTION("M grant the claim W nothing holds it") {
    // When the profiler is claimed with no other claim held
    auto ownership = ProfilerOwnership::TryAcquire();

    // Then the claim is granted
    REQUIRE(ownership);
  }

  SECTION("M refuse a second claim W the first is still held") {
    // When a second claim is attempted while the first is held
    auto first = ProfilerOwnership::TryAcquire();
    REQUIRE(first);
    auto second = ProfilerOwnership::TryAcquire();

    // Then it is refused
    REQUIRE(second == nullptr);
  }

  SECTION("M grant a new claim W the previous one is released") {
    // When the first claim is released
    auto first = ProfilerOwnership::TryAcquire();
    REQUIRE(first);
    first.reset();

    // Then the profiler can be claimed again
    auto second = ProfilerOwnership::TryAcquire();
    REQUIRE(second);
  }
}
