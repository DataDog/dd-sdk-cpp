// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/upload_scheduler.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cinttypes>
#include <memory>
#include <thread>
#include <utility>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/core.hpp"

#include "mock/clock.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/http_client.hpp"
#include "mock/tlv.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("UploadScheduler", "[unit]") {
  SECTION("M schedule and return feature W single feature scheduled") {
    // Given a scheduler with an upload cycle scheduled in 1 millisecond
    MockClock clock;
    UploadScheduler scheduler(clock);
    scheduler.Schedule(0xfeee0000, std::chrono::milliseconds(1));

    // When we wait for the next scheduled feature
    auto started_at = std::chrono::high_resolution_clock::now();
    auto result = scheduler.WaitForNext();
    auto elapsed = std::chrono::high_resolution_clock::now() - started_at;

    // Then we get the ID of the feature we scheduled
    REQUIRE(result.has_value());
    REQUIRE(*result == 0xfeee0000);

    // And somewhere between 0.5ms and 100ms has elapsed
    auto elapsed_ms = std::chrono::round<std::chrono::milliseconds>(elapsed);
    REQUIRE(elapsed_ms.count() >= 1);
    REQUIRE(elapsed_ms.count() <= 100);
  }

  SECTION("M return earliest feature W features scheduled at different times") {
    // Given a scheduler where 0xfeee0000 is scheduled in 30 seconds
    MockClock clock;
    UploadScheduler scheduler(clock);
    scheduler.Schedule(0xfeee0000, std::chrono::seconds(30));

    // And 0x1337beef is scheduled in 1 microsecond
    scheduler.Schedule(0x1337beef, std::chrono::microseconds(1));

    // When we wait for the next scheduled feature
    auto result = scheduler.WaitForNext();

    // Then we get 0x1337beef
    REQUIRE(result.has_value());
    REQUIRE(*result == 0x1337beef);
  }

  SECTION("M return nullopt W no features scheduled") {
    // Given a scheduler with no features scheduled
    MockClock clock;
    UploadScheduler scheduler(clock);

    // When we wait for the next scheduled feature
    auto started_at = std::chrono::high_resolution_clock::now();
    auto result = scheduler.WaitForNext();
    auto elapsed = std::chrono::high_resolution_clock::now() - started_at;

    // Then we get no value
    REQUIRE(!result.has_value());

    // And no blocking wait has occurred
    auto elapsed_ms = std::chrono::round<std::chrono::milliseconds>(elapsed);
    REQUIRE(elapsed_ms.count() == 0);
  }

  SECTION("M return immediately W scheduled time has already passed") {
    // Given a feature scheduled to run an upload cycle in 9 seconds
    MockClock clock;
    UploadScheduler scheduler(clock);
    scheduler.Schedule(0xfeee0000, std::chrono::seconds(9));

    // When 9 seconds or more has elapsed
    clock.FreezeAt(clock.Now());
    clock.Tick(std::chrono::seconds(10));

    // And we wait for the next scheduled feature
    auto started_at = std::chrono::high_resolution_clock::now();
    auto result = scheduler.WaitForNext();
    auto elapsed = std::chrono::high_resolution_clock::now() - started_at;

    // Then we get our feature
    REQUIRE(result.has_value());
    REQUIRE(*result == 0xfeee0000);

    // And no blocking wait has occurred
    auto elapsed_ms = std::chrono::round<std::chrono::milliseconds>(elapsed);
    REQUIRE(elapsed_ms.count() == 0);
  }

  SECTION("M return nullopt W Stop called during wait") {
    // Given a feature scheduled to run an upload cycle in 10 seconds
    MockClock clock;
    UploadScheduler scheduler(clock);
    scheduler.Schedule(0xfeee0000, std::chrono::seconds(10));

    // And a thread that's waiting for that 10-second delay to elapse
    std::atomic<int> num_elapsed{0};
    auto block_until_nullopt = [&]() {
      while (auto next = scheduler.WaitForNext()) {
        num_elapsed++;
      }
    };
    std::thread thread{block_until_nullopt};
    std::this_thread::sleep_for(std::chrono::microseconds(10));

    // When we stop scheduling and join on the thread
    auto started_at = std::chrono::high_resolution_clock::now();
    scheduler.Stop();
    thread.join();
    auto elapsed = std::chrono::high_resolution_clock::now() - started_at;

    // Then the thread exits immediately (typically <1ms, but fudge it to 20ms to allow
    // for variable CPU responsiveness in the test environment)
    auto elapsed_ms = std::chrono::round<std::chrono::milliseconds>(elapsed);
    REQUIRE(elapsed_ms.count() <= 20);

    // And the scheduled upload cycle does not take place
    REQUIRE(num_elapsed == 0);
  }
}
