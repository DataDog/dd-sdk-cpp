// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <cinttypes>
#include <optional>
#include <string>
#include <vector>

#include "datadog/impl/core/platform/clock.hpp"

using namespace datadog;

/**
 * Mock implementation of IClock. Uses std::chrono::system_clock, but may be manually
 * manipulated from tests.
 */
class MockClock : public platform::IClock {
 public:
  // Current timestamp at which we've been frozen, or 0 if running normally
  Timestamp frozen_at{};

  /**
   * Returns the current time as we've defined it in our test environment.
   */
  Timestamp Now() const override {
    if (frozen_at != Timestamp{}) {
      return frozen_at;
    }
    return std::chrono::system_clock::now();
  }

  /**
   * Freezes the clock at the given timestamp.
   */
  void FreezeAt(Timestamp value) {
    if (value != Timestamp{}) {
      frozen_at = value;
    }
  }

  /**
   * If frozen, increments the mock timestamp by the given amount. Has no effect if not
   * frozen.
   */
  void Tick(Duration delta) {
    if (frozen_at != Timestamp{}) {
      frozen_at += delta;
    }
  }

  // Convenience functions for setting up tests without chrono casts

  void FreezeAtMilliseconds(int64_t unix_time_ms) {
    FreezeAt(
        Timestamp{std::chrono::duration_cast<Duration>(
            std::chrono::milliseconds{unix_time_ms}
        )}
    );
  }

  void TickMilliseconds(int64_t delta_ms) {
    Tick(std::chrono::duration_cast<Duration>(std::chrono::milliseconds{delta_ms}));
  }
};
