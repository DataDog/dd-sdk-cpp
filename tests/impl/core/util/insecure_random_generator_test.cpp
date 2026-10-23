// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/util/insecure_random_generator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

using namespace datadog::impl;

TEST_CASE("InsecureRandomGenerator", "[unit][random]") {
  SECTION("M generate the configured value W range has one possible value") {
    InsecureRandomUInt64Generator generator(42, 42);

    REQUIRE(generator.NextValue() == 42);
  }

  SECTION("M generate values within the configured inclusive range") {
    constexpr uint64_t MINIMUM = 10;
    constexpr uint64_t MAXIMUM = 15;

    InsecureRandomUInt64Generator generator(MINIMUM, MAXIMUM);

    for (std::size_t iteration = 0; iteration < 1000; ++iteration) {
      const uint64_t value = generator.NextValue();
      REQUIRE(value >= MINIMUM);
      REQUIRE(value <= MAXIMUM);
    }
  }

  SECTION("M generate varied values") {
    constexpr std::size_t RUN_COUNT = 1000;
    constexpr std::size_t MINIMUM_UNIQUE_VALUES = RUN_COUNT * 95 / 100;

    InsecureRandomUInt64Generator generator;
    std::unordered_set<uint64_t> values;
    for (std::size_t iteration = 0; iteration < RUN_COUNT; ++iteration) {
      values.insert(generator.NextValue());
    }

    REQUIRE(values.size() >= MINIMUM_UNIQUE_VALUES);
  }
}
