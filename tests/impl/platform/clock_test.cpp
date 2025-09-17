// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "platform/clock.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

using namespace datadog;

// [platform-clock] tests validate the clock implementation.

TEST_CASE("Clock", "[unit][platform-clock]") {
  // Given a system clock interface
  auto clock = platform::Clock::Init();
  REQUIRE(clock != nullptr);

  SECTION("M return valid system time in nanoseconds W sampled") {
    // When we read the system time
    platform::Timestamp now = clock->Now();

    // Then it gives us a value that's after January 1, 2020, 00:00:00 UTC
    const platform::Timestamp start_of_2020{
        platform::Duration{std::chrono::seconds(1577836800)}
    };
    REQUIRE(now.time_since_epoch() > start_of_2020.time_since_epoch());

    // And before December 31, 2120, 23:59:59 UTC
    const platform::Timestamp end_of_2120{
        platform::Duration{std::chrono::seconds(4765132799)}
    };
    REQUIRE(now.time_since_epoch() < end_of_2120.time_since_epoch());
  }

  SECTION("M return values with at least millisecond precision W sampled") {
    // When we take an initial sample of system time
    const platform::Timestamp start = clock->Now();

    platform::Duration observed_interval{0};
    const int MAX_ITERATIONS = 5000;
    for (int i = 0; i < MAX_ITERATIONS; i++) {
      // And we continually read the clock in a loop until the timestamp increases
      const platform::Timestamp now = clock->Now();
      if (now.time_since_epoch() > start.time_since_epoch()) {
        // NOTE: The SDK does NOT assume that the clock is monotonic. It may in
        // fact jump around at runtime, but the assumption that it will increase
        // steadily within this unit test is safe enough
        observed_interval = now - start;
        break;
      }
      REQUIRE(now.time_since_epoch() == start.time_since_epoch());
    }

    // Then we will see the timestamp increase at some point in our loop
    REQUIRE(observed_interval.count() > 0);

    // And the duration we measured will be no greater than a millisecond
    const platform::Duration one_millisecond{std::chrono::milliseconds(1)};
    REQUIRE(observed_interval <= one_millisecond);
  }
}
