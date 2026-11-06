// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/data_points_reader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <optional>

#include "mock/clock.hpp"
#include "support/catch_timestamp.hpp"

using namespace datadog;
using namespace datadog::impl;

namespace {
class ScriptedReader final : public DataPointsReader<double> {
 public:
  explicit ScriptedReader(const platform::IClock& clock)
      : DataPointsReader(clock, std::chrono::seconds(1)) {}

  std::optional<double> next;
  std::function<void()> on_read;

 protected:
  std::optional<double> ReadValue() override {
    if (on_read) {
      on_read();
    }
    return next;
  }
};
}  // namespace

TEST_CASE("DataPointsReader", "[unit][rum][timeseries]") {
  SECTION("M stamp value with the clock W value is read") {
    MockClock clock;
    clock.FreezeAtMilliseconds(1000);
    ScriptedReader reader(clock);

    reader.next = 1.5;
    auto point = reader.Read();
    REQUIRE(point.has_value());
    CHECK(point->value == 1.5);
    CHECK(point->timestamp == clock.Now());

    clock.TickMilliseconds(250);
    reader.next = 2.5;
    point = reader.Read();
    REQUIRE(point.has_value());
    CHECK(point->value == 2.5);
    CHECK(point->timestamp == clock.Now());
  }

  SECTION("M return no point W no value is available") {
    MockClock clock;
    clock.FreezeAtMilliseconds(1000);
    ScriptedReader reader(clock);
    reader.next = std::nullopt;
    CHECK_FALSE(reader.Read().has_value());
  }

  SECTION("M stamp the time before reading W read is slow") {
    MockClock clock;
    clock.FreezeAtMilliseconds(1000);
    ScriptedReader reader(clock);
    reader.next = 1.0;
    reader.on_read = [&] { clock.TickMilliseconds(500); };  // a slow read

    const auto point = reader.Read();
    REQUIRE(point.has_value());
    CHECK(point->timestamp == Timestamp{std::chrono::milliseconds(1000)});
  }

  SECTION("M return the interval W constructed with it") {
    MockClock clock;
    const ScriptedReader reader(clock);
    CHECK(reader.Interval() == std::chrono::seconds(1));
  }
}
