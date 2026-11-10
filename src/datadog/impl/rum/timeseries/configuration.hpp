// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <cstddef>
#include <set>

#include "datadog/timestamp.hpp"

namespace datadog::impl {

/** Kinds of timeseries that can be collected. */
enum class TimeseriesType {
  Cpu,
  Memory,
};

inline const char* TimeseriesType_ToString(TimeseriesType value) {
  switch (value) {
    case TimeseriesType::Cpu:
      return "cpu";
    case TimeseriesType::Memory:
      return "memory";
  }
  return "";
}

/**
 * Internal configuration of timeseries collection. Deliberately not part of the public
 * RumConfig.
 */
struct TimeseriesConfig final {
  /** Which timeseries to collect; at most one pipeline is created per type. */
  std::set<TimeseriesType> types{TimeseriesType::Cpu, TimeseriesType::Memory};
  /** Number of data points per timeseries event. */
  size_t buffer_size = 120;
  /** How often each timeseries is sampled. */
  Duration sampling_interval = std::chrono::milliseconds(1000);
};

}  // namespace datadog::impl
