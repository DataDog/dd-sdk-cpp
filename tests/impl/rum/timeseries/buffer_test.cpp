// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/buffer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "support/catch_timestamp.hpp"

using namespace datadog;
using namespace datadog::impl;

namespace {
DataPoint<int> Point(int value) {
  return DataPoint<int>{Timestamp{std::chrono::milliseconds(value)}, value};
}
}  // namespace

TEST_CASE("Buffer", "[unit][rum][timeseries]") {
  SECTION("M become full W capacity is reached") {
    Buffer<int> buffer(3);
    CHECK_FALSE(buffer.IsFull());
    buffer.Add(Point(1));
    buffer.Add(Point(2));
    CHECK_FALSE(buffer.IsFull());
    buffer.Add(Point(3));
    CHECK(buffer.IsFull());
  }

  SECTION("M return points in order and reset W drained") {
    Buffer<int> buffer(3);
    for (int i = 1; i <= 3; i++) {
      buffer.Add(Point(i));
    }

    const auto drained = buffer.Drain();
    REQUIRE(drained.size() == 3);
    for (int i = 0; i < 3; i++) {
      CHECK(drained[static_cast<size_t>(i)].value == i + 1);
      CHECK(drained[static_cast<size_t>(i)].timestamp == Point(i + 1).timestamp);
    }
    CHECK_FALSE(buffer.IsFull());
    CHECK(buffer.Drain().empty());
  }

  SECTION("M accept points W drained") {
    Buffer<int> buffer(2);
    buffer.Add(Point(1));
    buffer.Add(Point(2));
    CHECK(buffer.Drain().size() == 2);

    buffer.Add(Point(3));
    CHECK_FALSE(buffer.IsFull());
    buffer.Add(Point(4));
    CHECK(buffer.IsFull());

    const auto drained = buffer.Drain();
    REQUIRE(drained.size() == 2);
    CHECK(drained[0].value == 3);
    CHECK(drained[1].value == 4);
  }
}
