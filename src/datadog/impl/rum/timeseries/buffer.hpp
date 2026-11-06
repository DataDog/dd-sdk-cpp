// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "datadog/impl/rum/timeseries/data_point.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

/**
 * Fixed-capacity accumulator of data points.
 *
 * Not thread-safe: the owner (Pipeline) guards it with its own mutex. Add() does not
 * enforce the capacity; the owner is expected to Drain() as soon as IsFull() is true.
 *
 * A capacity of 0 is a contract violation (asserted); without asserts the buffer
 * behaves like capacity 1, since a buffer holding a point is already full.
 */
template <typename T>
class Buffer final {
 public:
  /**
   * @param capacity Number of points at which the buffer is considered full; must be
   *  positive. Storage for that many points is reserved up front.
   */
  explicit Buffer(size_t capacity) : _capacity(capacity) {
    DATADOG_ASSERT(capacity > 0, "timeseries buffer capacity must be positive");
    _points.reserve(_capacity);
  }

  void Add(DataPoint<T> point) { _points.push_back(std::move(point)); }

  /** Returns true once the number of buffered points reaches the capacity. */
  bool IsFull() const { return _points.size() >= _capacity; }

  /**
   * Moves all buffered points out, in insertion order, leaving the buffer empty with
   * its storage re-reserved for the next batch.
   */
  std::vector<DataPoint<T>> Drain() {
    std::vector<DataPoint<T>> drained;
    drained.swap(_points);
    _points.reserve(_capacity);
    return drained;
  }

 private:
  size_t _capacity;
  std::vector<DataPoint<T>> _points;
};

}  // namespace datadog::impl
