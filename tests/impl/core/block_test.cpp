// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/block.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace datadog::impl;

TEST_CASE("QuantizeBufferSize", "[unit]") {
  SECTION("M clamp to 256 W value below 256") {
    REQUIRE(QuantizeBufferSize(1) == 256);
    REQUIRE(QuantizeBufferSize(100) == 256);
    REQUIRE(QuantizeBufferSize(255) == 256);
  }

  SECTION("M return exact value W value is already power of two under 64KB") {
    REQUIRE(QuantizeBufferSize(256) == 256);
    REQUIRE(QuantizeBufferSize(512) == 512);
    REQUIRE(QuantizeBufferSize(1024) == 1024);
    REQUIRE(QuantizeBufferSize(2048) == 2048);
    REQUIRE(QuantizeBufferSize(32768) == 32768);
    REQUIRE(QuantizeBufferSize(65536) == 65536);
  }

  SECTION("M round up to nearest power of two W value under 64KB") {
    REQUIRE(QuantizeBufferSize(257) == 512);
    REQUIRE(QuantizeBufferSize(513) == 1024);
    REQUIRE(QuantizeBufferSize(1025) == 2048);
    REQUIRE(QuantizeBufferSize(2049) == 4096);
    REQUIRE(QuantizeBufferSize(4097) == 8192);
    REQUIRE(QuantizeBufferSize(8193) == 16384);
    REQUIRE(QuantizeBufferSize(16385) == 32768);
    REQUIRE(QuantizeBufferSize(32769) == 65536);
  }

  SECTION("M use power-of-two logic W value equals 64KB") {
    const size_t threshold = 64 * 1024;
    REQUIRE(QuantizeBufferSize(threshold) == threshold);
  }

  SECTION("M use 16KB increments W value above 64KB") {
    const size_t increment = 16 * 1024;
    const size_t base = 64 * 1024;

    REQUIRE(QuantizeBufferSize(base + 1) == base + increment);
    REQUIRE(QuantizeBufferSize(base + increment - 1) == base + increment);
    REQUIRE(QuantizeBufferSize(base + increment) == base + increment);
    REQUIRE(QuantizeBufferSize(base + increment + 1) == base + 2 * increment);

    REQUIRE(QuantizeBufferSize(100 * 1024) == 112 * 1024);
    REQUIRE(QuantizeBufferSize(200 * 1024) == 208 * 1024);
    REQUIRE(QuantizeBufferSize(500 * 1024) == 512 * 1024);
  }

  SECTION("M clamp to 256 W value is zero") { REQUIRE(QuantizeBufferSize(0) == 256); }

  SECTION("M handle correctly W value is very large") {
    const size_t large_value = 1024 * 1024;
    const size_t expected = ((large_value + 16 * 1024 - 1) / (16 * 1024)) * (16 * 1024);
    REQUIRE(QuantizeBufferSize(large_value) == expected);

    const size_t very_large = SIZE_MAX - 100000;
    size_t result = QuantizeBufferSize(very_large);
    REQUIRE(result >= very_large);
  }

  SECTION("M always return value >= input W any input") {
    for (size_t i = 1; i <= 100000; i += 1000) {
      REQUIRE(QuantizeBufferSize(i) >= i);
    }
  }

  SECTION("M return power of two W input <= 64KB") {
    auto is_power_of_two = [](size_t n) { return n > 0 && (n & (n - 1)) == 0; };

    for (size_t i = 1; i <= 64 * 1024; i += 1000) {
      size_t result = QuantizeBufferSize(i);
      REQUIRE(is_power_of_two(result));
      REQUIRE(result >= 256);
    }
  }

  SECTION("M return multiple of 16KB W input > 64KB") {
    for (size_t i = 64 * 1024 + 1; i <= 200 * 1024; i += 5000) {
      size_t result = QuantizeBufferSize(i);
      REQUIRE(result % (16 * 1024) == 0);
    }
  }
}
