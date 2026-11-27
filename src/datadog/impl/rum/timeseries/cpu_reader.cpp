// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/cpu_reader.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

CpuReader::CpuReader(
    const platform::IClock& clock,
    Duration interval,
    std::shared_ptr<const platform::ICpuInfoProvider> cpu_info_provider,
    SteadyNow steady_now
)
    : DataPointsReader(clock, interval),
      _cpu_info_provider(std::move(cpu_info_provider)),
      _steady_now(std::move(steady_now)) {
  DATADOG_ASSERT(
      _cpu_info_provider != nullptr, "CPU reader requires a CPU info provider"
  );
  DATADOG_ASSERT(_steady_now != nullptr, "CPU reader requires a steady clock");
}

std::optional<double> CpuReader::ReadValue() {
  const std::chrono::steady_clock::time_point now = _steady_now();
  const std::optional<Duration> cpu_time = _cpu_info_provider->CpuTime();
  const std::optional<uint32_t> cpu_count = _cpu_info_provider->LogicalCpuCount();
  if (!cpu_time.has_value() || !cpu_count.has_value() || *cpu_count == 0) {
    // Keep the baseline: the next value then covers this sample too
    return std::nullopt;
  }

  const std::optional<Baseline> previous =
      std::exchange(_baseline, Baseline{now, *cpu_time});
  if (!previous.has_value()) {
    return std::nullopt;
  }

  const auto elapsed = now - previous->at;
  if (elapsed <= std::chrono::steady_clock::duration::zero() ||
      elapsed > 2 * Interval()) {
    return std::nullopt;
  }

  // A negative delta (counter reset) clamps to 0
  const double cpu_ns =
      std::chrono::duration<double, std::nano>(*cpu_time - previous->cpu_time).count();
  const double elapsed_ns = std::chrono::duration<double, std::nano>(elapsed).count();
  const double usage = cpu_ns / elapsed_ns / static_cast<double>(*cpu_count) * 100.0;
  if (!std::isfinite(usage)) {
    return std::nullopt;
  }
  return std::clamp(usage, 0.0, 100.0);
}

}  // namespace datadog::impl
