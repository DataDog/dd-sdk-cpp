#pragma once

#include <chrono>
#include <cinttypes>
#include <optional>
#include <string>
#include <vector>

#include "platform/clock.hpp"

using namespace datadog;

/**
 * Mock implementation of IClock. Uses std::chrono::system_clock, but may be manually
 * manipulated from tests.
 */
class MockClock : public platform::IClock {
 public:
  // Current timestamp at which we've been frozen, or 0 if running normally
  platform::Timestamp frozen_at{};

  /**
   * Returns the current time as we've defined it in our test environment.
   */
  platform::Timestamp Now() const override {
    if (frozen_at != platform::Timestamp{}) {
      return frozen_at;
    }
    return std::chrono::system_clock::now();
  }

  /**
   * Freezes the clock at the given timestamp.
   */
  void FreezeAt(platform::Timestamp value) {
    if (value != platform::Timestamp{}) {
      frozen_at = value;
    }
  }

  /**
   * If frozen, increments the mock timestamp by the given amount. Has no effect if not
   * frozen.
   */
  void Tick(platform::Duration delta) {
    if (frozen_at != platform::Timestamp{}) {
      frozen_at += delta;
    }
  }

  // Convenience functions for setting up tests without chrono casts

  void FreezeAtMilliseconds(int64_t unix_time_ms) {
    FreezeAt(
        platform::Timestamp{std::chrono::duration_cast<platform::Duration>(
            std::chrono::milliseconds{unix_time_ms}
        )}
    );
  }

  void TickMilliseconds(int64_t delta_ms) {
    Tick(
        std::chrono::duration_cast<platform::Duration>(
            std::chrono::milliseconds{delta_ms}
        )
    );
  }
};
