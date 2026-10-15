// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/span_id_generator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <unordered_set>

using namespace datadog::impl;

TEST_CASE("DefaultSpanIDGenerator", "[unit][tracing]") {
  SECTION("M generate IDs with random bits") {
    constexpr std::size_t RUN_COUNT = 1000;
    constexpr std::size_t MINIMUM_UNIQUE_VALUES = RUN_COUNT * 95 / 100;

    DefaultSpanIDGenerator generator;

    std::unordered_set<std::string> values;
    for (std::size_t iteration = 0; iteration < RUN_COUNT; ++iteration) {
      const SpanID id = generator.Generate();
      values.insert(id.ToHexString());
    }

    // The probability of this test failing, assuming a good quality, truly uniform
    // distribution is ~2.31e-760. So if it fails, there's probably a real bug.
    REQUIRE(values.size() >= MINIMUM_UNIQUE_VALUES);
  }
}
