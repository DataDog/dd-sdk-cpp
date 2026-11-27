// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/cpu_reader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/platform/cpu_info_provider.hpp"

#include "mock/clock.hpp"
#include "support/catch_timestamp.hpp"

using namespace datadog;
using namespace datadog::impl;
using Catch::Matchers::WithinRel;

namespace {

constexpr Duration INTERVAL = std::chrono::seconds(1);

/** Raw values the reader sees; tests change them between samples. */
struct FakeCpu final {
  std::optional<Duration> cpu_time{std::chrono::seconds(10)};
  std::optional<uint32_t> cpu_count{1};
  std::chrono::steady_clock::time_point now{std::chrono::hours(1)};

  /** Moves the steady clock forward by `elapsed` and the CPU time by `used`. */
  void Advance(Duration elapsed, Duration used) {
    now += elapsed;
    if (cpu_time.has_value()) {
      *cpu_time += used;
    }
  }
};

/** Serves the values of a FakeCpu, which the test keeps changing. */
class FakeCpuInfoProvider final : public platform::ICpuInfoProvider {
 public:
  explicit FakeCpuInfoProvider(std::shared_ptr<const FakeCpu> cpu)
      : _cpu(std::move(cpu)) {}

  std::optional<Duration> CpuTime() const override { return _cpu->cpu_time; }
  std::optional<uint32_t> LogicalCpuCount() const override { return _cpu->cpu_count; }

 private:
  std::shared_ptr<const FakeCpu> _cpu;
};

struct ReaderFixture final {
  MockClock clock;
  std::shared_ptr<FakeCpu> cpu = std::make_shared<FakeCpu>();
  CpuReader reader{
      clock,
      INTERVAL,
      std::make_shared<FakeCpuInfoProvider>(cpu),
      [cpu = cpu] { return cpu->now; },
  };

  ReaderFixture() { clock.FreezeAtMilliseconds(1'000'000); }

  std::optional<double> Read() {
    const std::optional<DataPoint<double>> point = reader.Read();
    if (!point.has_value()) {
      return std::nullopt;
    }
    return point->value;
  }

  /** Takes the baseline sample, which never produces a value. */
  void Prime() { REQUIRE_FALSE(Read().has_value()); }

  std::optional<double> ReadAfter(Duration elapsed, Duration used) {
    cpu->Advance(elapsed, used);
    return Read();
  }
};

}  // namespace

TEST_CASE("CpuReader", "[unit][rum][timeseries]") {
  ReaderFixture f;

  SECTION("M return no value W first sample") { CHECK_FALSE(f.Read().has_value()); }

  SECTION("M return the usage of one CPU W one CPU is available") {
    f.Prime();
    const std::optional<double> value =
        f.ReadAfter(INTERVAL, std::chrono::milliseconds(250));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(25.0));
  }

  SECTION("M divide by the CPU count W several CPUs are available") {
    f.cpu->cpu_count = 4;
    f.Prime();
    const std::optional<double> value = f.ReadAfter(INTERVAL, std::chrono::seconds(2));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(50.0));
  }

  SECTION("M average over the elapsed time W samples are not evenly spaced") {
    f.Prime();
    const std::optional<double> value =
        f.ReadAfter(std::chrono::milliseconds(1500), std::chrono::milliseconds(300));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(20.0));
  }

  SECTION("M return 0 W no CPU time was used") {
    f.Prime();
    const std::optional<double> value = f.ReadAfter(INTERVAL, Duration::zero());
    REQUIRE(value.has_value());
    CHECK(*value == 0.0);
  }

  SECTION("M clamp to 100 W usage exceeds the capacity") {
    f.Prime();
    const std::optional<double> value = f.ReadAfter(INTERVAL, std::chrono::seconds(3));
    REQUIRE(value.has_value());
    CHECK(*value == 100.0);
  }

  SECTION("M clamp to 0 W the CPU time goes backwards") {
    f.Prime();
    const std::optional<double> value = f.ReadAfter(INTERVAL, -std::chrono::seconds(1));
    REQUIRE(value.has_value());
    CHECK(*value == 0.0);
  }

  SECTION("M use the current CPU count W it changes between samples") {
    f.cpu->cpu_count = 2;
    f.Prime();
    std::optional<double> value = f.ReadAfter(INTERVAL, std::chrono::seconds(1));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(50.0));

    f.cpu->cpu_count = 4;
    value = f.ReadAfter(INTERVAL, std::chrono::seconds(1));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(25.0));
  }

  SECTION("M return a value W exactly twice the interval has passed") {
    f.Prime();
    const std::optional<double> value =
        f.ReadAfter(2 * INTERVAL, std::chrono::seconds(1));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(50.0));
  }

  SECTION(
      "M return no value and move the baseline W more than twice the interval passed"
  ) {
    f.Prime();
    CHECK_FALSE(f.ReadAfter(
                     2 * INTERVAL + std::chrono::milliseconds(1),
                     std::chrono::seconds(2)
    )
                    .has_value());

    // The next value only covers the time since the skipped sample
    const std::optional<double> value =
        f.ReadAfter(INTERVAL, std::chrono::milliseconds(100));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(10.0));
  }

  SECTION("M return no value W no steady time has passed") {
    f.Prime();
    CHECK_FALSE(f.ReadAfter(Duration::zero(), std::chrono::seconds(1)).has_value());
  }

  SECTION("M keep the baseline W the CPU time cannot be read") {
    f.Prime();
    const Duration last = *f.cpu->cpu_time;
    f.cpu->cpu_time.reset();
    CHECK_FALSE(f.ReadAfter(INTERVAL, Duration::zero()).has_value());

    // The next value spans both intervals
    f.cpu->cpu_time = last + std::chrono::seconds(1);
    const std::optional<double> value = f.ReadAfter(INTERVAL, Duration::zero());
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(50.0));
  }

  SECTION("M keep the baseline W the CPU count cannot be read") {
    f.Prime();
    f.cpu->cpu_count.reset();
    CHECK_FALSE(f.ReadAfter(INTERVAL, std::chrono::milliseconds(500)).has_value());

    f.cpu->cpu_count = 1;
    const std::optional<double> value =
        f.ReadAfter(INTERVAL, std::chrono::milliseconds(500));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(50.0));
  }

  SECTION("M keep the baseline W the CPU count is 0") {
    f.Prime();
    f.cpu->cpu_count = 0;
    CHECK_FALSE(f.ReadAfter(INTERVAL, std::chrono::milliseconds(500)).has_value());

    f.cpu->cpu_count = 1;
    const std::optional<double> value =
        f.ReadAfter(INTERVAL, std::chrono::milliseconds(500));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(50.0));
  }

  SECTION("M return no value W the first sample cannot be read") {
    f.cpu->cpu_time.reset();
    CHECK_FALSE(f.Read().has_value());

    // The baseline is only taken once a sample succeeds
    f.cpu->cpu_time = std::chrono::seconds(10);
    CHECK_FALSE(f.ReadAfter(INTERVAL, Duration::zero()).has_value());
    const std::optional<double> value =
        f.ReadAfter(INTERVAL, std::chrono::milliseconds(500));
    REQUIRE(value.has_value());
    CHECK_THAT(*value, WithinRel(50.0));
  }

  SECTION("M stamp the value with the wall clock W a value is read") {
    f.Prime();
    f.clock.TickMilliseconds(1000);
    f.cpu->Advance(INTERVAL, std::chrono::milliseconds(500));
    const std::optional<DataPoint<double>> point = f.reader.Read();
    REQUIRE(point.has_value());
    CHECK(point->timestamp == f.clock.Now());
  }

  SECTION("M report the configured interval W asked") {
    CHECK(f.reader.Interval() == INTERVAL);
  }
}

TEST_CASE("CpuReader with the platform provider", "[unit][rum][timeseries]") {
  SECTION("M return a value in range W the process is sampled twice") {
    MockClock clock;
    CpuReader reader(
        clock, std::chrono::seconds(10), std::make_shared<platform::CpuInfoProvider>()
    );
    REQUIRE_FALSE(reader.Read().has_value());

    // The steady clock may not have moved yet after a single sample
    std::optional<DataPoint<double>> point;
    for (int i = 0; i < 1000 && !point.has_value(); i++) {
      point = reader.Read();
    }
    REQUIRE(point.has_value());
    CHECK(point->value >= 0.0);
    CHECK(point->value <= 100.0);
  }
}
