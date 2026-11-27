// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/core/platform/cpu_info_provider.hpp"
#include "datadog/impl/rum/timeseries/data_points_reader.hpp"

namespace datadog::impl {

/** Reads a steady clock: unlike the wall clock, it cannot jump. */
using SteadyNow = std::function<std::chrono::steady_clock::time_point()>;

/**
 * Samples the CPU usage of the process, as a percentage of the capacity of every
 * online logical CPU of the machine, in [0, 100]. Each value is the average over the
 * time since the previous sample:
 *
 *   clamp(Δcpu_time / Δsteady_time / logical_cpus × 100, 0, 100)
 *
 * No value (std::nullopt) is produced:
 *  - for the first sample, which only records the baseline;
 *  - when more than twice the interval has passed since the previous sample (e.g. the
 *    machine was suspended); the baseline still moves to this sample;
 *  - when the CPU time or the CPU count cannot be read, or the CPU count is 0; the
 *    baseline stays, so the next value covers the failed sample too.
 *
 * The CPU count is read on every sample, to follow CPUs going online or offline.
 * Container CPU quotas (cgroups) are not taken into account.
 */
class CpuReader final : public DataPointsReader<double> {
 public:
  /**
   * @param clock Stamps the sampled values; must outlive the reader.
   * @param interval How often the value is sampled; must be positive. Also bounds the
   *  time between two samples that still produce a value.
   * @param cpu_info_provider Reads the CPU counters of the process; must be set.
   * Shared, so it lives as long as the readers using it.
   * @param steady_now Measures the time between samples; must be set.
   */
  CpuReader(
      const platform::IClock& clock,
      Duration interval,
      std::shared_ptr<const platform::ICpuInfoProvider> cpu_info_provider,
      SteadyNow steady_now = std::chrono::steady_clock::now
  );

 protected:
  std::optional<double> ReadValue() override;

 private:
  struct Baseline final {
    std::chrono::steady_clock::time_point at;
    Duration cpu_time;
  };

  const std::shared_ptr<const platform::ICpuInfoProvider> _cpu_info_provider;
  const SteadyNow _steady_now;
  std::optional<Baseline> _baseline;
};

}  // namespace datadog::impl
