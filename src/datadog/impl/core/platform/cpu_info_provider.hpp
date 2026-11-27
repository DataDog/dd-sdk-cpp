// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <optional>

#include "datadog/timestamp.hpp"

namespace datadog::platform {

/**
 * CPU usage counters of the current process, read from the operating system.
 *
 * Both methods are thread-safe, never throw, and return std::nullopt if the value
 * cannot be read.
 */
class ICpuInfoProvider {
 public:
  ICpuInfoProvider() = default;
  ICpuInfoProvider(const ICpuInfoProvider&) = default;
  ICpuInfoProvider& operator=(const ICpuInfoProvider&) = default;
  ICpuInfoProvider(ICpuInfoProvider&&) = default;
  ICpuInfoProvider& operator=(ICpuInfoProvider&&) = default;
  virtual ~ICpuInfoProvider() = default;

  /**
   * Returns the total CPU time (user and kernel) consumed by every thread of the
   * current process since it started, including threads that have already exited.
   * Monotonic for the lifetime of the process.
   */
  virtual std::optional<Duration> CpuTime() const = 0;

  /**
   * Returns the number of online logical CPUs of the machine: the capacity the CPU
   * usage is measured against. Never returns 0.
   */
  virtual std::optional<uint32_t> LogicalCpuCount() const = 0;
};

/**
 * The implementation for the platform the SDK runs on, defined in
 * cpu_info_provider_<platform>.cpp.
 */
class CpuInfoProvider final : public ICpuInfoProvider {
 public:
  std::optional<Duration> CpuTime() const override;
  std::optional<uint32_t> LogicalCpuCount() const override;
};

}  // namespace datadog::platform
