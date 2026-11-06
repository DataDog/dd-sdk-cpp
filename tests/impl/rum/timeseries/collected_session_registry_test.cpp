// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/collected_session_registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

using namespace datadog;
using namespace datadog::impl;

namespace {
UUID Session(uint8_t n) {
  const uint8_t bytes[16] = {n, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
  return UUID(bytes);
}
}  // namespace

TEST_CASE("CollectedTimeseriesSessionRegistry", "[unit][rum][timeseries]") {
  SECTION("M remember session W timeseries collected for it") {
    CollectedTimeseriesSessionRegistry registry;
    CHECK_FALSE(registry.IsTimeseriesCollected(Session(1)));

    registry.SetTimeseriesCollected(Session(1));
    registry.SetTimeseriesCollected(Session(1));
    CHECK(registry.IsTimeseriesCollected(Session(1)));
    CHECK_FALSE(registry.IsTimeseriesCollected(Session(2)));
  }

  SECTION("M keep only the most recently used sessions W more than MAX_SESSIONS") {
    static_assert(CollectedTimeseriesSessionRegistry::MAX_SESSIONS == 3);
    CollectedTimeseriesSessionRegistry registry;
    registry.SetTimeseriesCollected(Session(1));
    registry.SetTimeseriesCollected(Session(2));
    registry.SetTimeseriesCollected(Session(3));
    registry.SetTimeseriesCollected(Session(1));  // 1 becomes the most recently used
    registry.SetTimeseriesCollected(Session(4));  // evicts 2, the least recently used

    CHECK(registry.IsTimeseriesCollected(Session(1)));
    CHECK_FALSE(registry.IsTimeseriesCollected(Session(2)));
    CHECK(registry.IsTimeseriesCollected(Session(3)));
    CHECK(registry.IsTimeseriesCollected(Session(4)));
  }
}
