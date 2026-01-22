// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include "datadog/timestamp.hpp"

namespace datadog::platform {

/**
 * Interface for the system clock.
 *
 * Timestamps sampled from IClock MUST have millisecond precision, at a minimum.
 *
 * Reading the system clock is assumed to be thread-safe.
 */
class IClock {
 protected:
  IClock() = default;

 public:
  virtual ~IClock() = default;

  // Copyable and movable: has no state
  IClock(const IClock&) = default;
  IClock& operator=(const IClock&) = default;
  IClock(IClock&&) = default;
  IClock& operator=(IClock&&) = default;

  /**
   * Returns the elapsed wall-clock time since the Unix epoch, i.e. since midnight
   * January 1, 1970, UTC, in nanoseconds.
   */
  virtual Timestamp Now() const = 0;
};

namespace Clock {
/**
 * Creates an IClock interface used to read to the system clock.
 */
std::unique_ptr<IClock> Init();
};  // namespace Clock

}  // namespace datadog::platform
