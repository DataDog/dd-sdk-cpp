// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <ratio>

#include "datadog/impl/types/assert.hpp"
#include "datadog/impl/types/json.hpp"

namespace datadog::impl {

/**
 * IsoTimestamp allows JSON-serializable struct types to be explicit about the timestamp
 * representation they use for a given member: ISO-8601 is the default format when
 * encoding a datadog::Timestamp value as JSON.
 */
using IsoTimestamp = Timestamp;

/**
 * Wrapper for a datadog::Timestamp value that is serialized to JSON as an integer count
 * of (milliseconds|nanoseconds|etc.) since the Unix epoch, where the target unit is
 * defined by `Period`.
 */
template <typename Period>
struct IntegerTimestamp {
  Timestamp value;

  // Use the same integer representation as datadog::Timestamp, i.e. int64_t
  using Rep = Timestamp::rep;

  // Alow implicit conversion and assignment from datadog::Timestamp
  IntegerTimestamp() {}
  // NOLINTNEXTLINE(google-explicit-constructor)
  IntegerTimestamp(const Timestamp& in_value) : value(in_value) {}
  IntegerTimestamp& operator=(const Timestamp& in_value) {
    value = in_value;
    return *this;
  }

  /**
   * Returns the currently-held timestamp value as an integer representing ticks since
   * the Unix epoch in the unit specified by `Period`.
   */
  Rep Count() const {
    using Target = std::chrono::duration<Rep, Period>;
    Target duration = std::chrono::duration_cast<Target>(value.time_since_epoch());
    return duration.count();
  }
};

template <typename Period>
size_t GetJsonSize(const IntegerTimestamp<Period>& value) {
  const auto count = value.Count();
  return GetJsonSize(count);
}

template <typename Period>
size_t WriteJson(char* dst, size_t n, const IntegerTimestamp<Period>& value) {
  const auto count = value.Count();
  return WriteJson(dst, n, count);
}

/**
 * Holds a `datadog::Timestamp` value; serialized to JSON as a number representing
 * elapsed nanoseconds since the Unix epoch.
 */
using NanoTimestamp = IntegerTimestamp<std::nano>;

/**
 * Holds a `datadog::Timestamp` value; serialized to JSON as a number representing
 * elapsed milliseconds since the Unix epoch.
 */
using MilliTimestamp = IntegerTimestamp<std::milli>;

}  // namespace datadog::impl
