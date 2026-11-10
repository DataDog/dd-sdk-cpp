// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <utility>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/rum/timeseries/data_point.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

/**
 * Source of timeseries values, sampled periodically by a Pipeline.
 *
 * Threading: Read() and ReadValue() are only ever called from the scheduler thread, and
 * may perform blocking I/O (e.g. reading from /proc). Interval() may be called from any
 * thread: the interval is fixed at construction.
 *
 * Contracts:
 *  - A reader must not block indefinitely: it shares the single scheduler thread with
 *    every other scheduled task, and a blocked Read() blocks SDK Stop (which joins that
 *    thread).
 *  - ReadValue() must not throw: the SDK may be built with `-fno-exceptions`.
 *  - The destructor may run on any thread (the last reference to the pipeline owning
 *    the reader can be released anywhere).
 *  - The timestamp is the wall-clock time at the start of the read. Consecutive points
 * may therefore be non-monotonic or have gaps; consumers must not assume equal spacing.
 */
template <typename T>
class DataPointsReader {
 public:
  virtual ~DataPointsReader() = default;

  /** How often the value should be sampled. */
  Duration Interval() const { return _interval; }

  /**
   * Samples the value and stamps it with the time at which the read started (the clock
   * is read once per call, before ReadValue(), even if no value turns out to be
   * available). Returns std::nullopt if no value is currently available.
   */
  std::optional<DataPoint<T>> Read() {
    // Stamp before reading, so a slow read does not shift the point's time
    const Timestamp timestamp = _clock->Now();
    std::optional<T> value = ReadValue();
    if (!value.has_value()) {
      return std::nullopt;
    }
    return DataPoint<T>{timestamp, std::move(*value)};
  }

 protected:
  /**
   * @param clock Stamps the sampled values; must outlive the reader.
   * @param interval How often the value should be sampled; must be positive.
   */
  DataPointsReader(const platform::IClock& clock, Duration interval)
      : _clock(&clock), _interval(interval) {
    DATADOG_ASSERT(
        interval > Duration::zero(), "timeseries sampling interval must be positive"
    );
  }

  // Polymorphic base: copying/moving through a base reference would slice
  DataPointsReader(const DataPointsReader&) = delete;
  DataPointsReader& operator=(const DataPointsReader&) = delete;
  DataPointsReader(DataPointsReader&&) = delete;
  DataPointsReader& operator=(DataPointsReader&&) = delete;

  /** Reads the raw value, or returns std::nullopt if it is unavailable. */
  virtual std::optional<T> ReadValue() = 0;

 private:
  const platform::IClock* _clock;
  const Duration _interval;
};

}  // namespace datadog::impl
