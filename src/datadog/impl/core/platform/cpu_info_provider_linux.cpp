// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <time.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <optional>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/platform/cpu_info_provider.hpp"

namespace datadog::platform {

// See https://man7.org/linux/man-pages/man2/clock_gettime.2.html
std::optional<Duration> CpuInfoProvider::CpuTime() const {
  // Covers every thread of the process, exited ones included (glibc and musl)
  timespec ts{};
  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) {
    return std::nullopt;
  }
  // A timespec is whole seconds plus the remaining nanoseconds (0 to 999'999'999)
  return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
}

// See https://man7.org/linux/man-pages/man3/sysconf.3.html and
// https://docs.kernel.org/admin-guide/cputopology.html
std::optional<uint32_t> CpuInfoProvider::LogicalCpuCount() const {
  // Capacity of the whole machine, like Windows (active processors) and macOS
  // (hw.activecpu): _SC_NPROCESSORS_ONLN, the CPUs that are online.
  // Alternatives: _SC_NPROCESSORS_CONF (what Android's availableProcessors() uses)
  // counts absent hot-pluggable slots, common on VMs, so usage would be underestimated;
  // sched_getaffinity() is only this process's CPUs and fails above 1024 CPUs.
  const auto online = sysconf(_SC_NPROCESSORS_ONLN);
  if (online > 0) {
    return static_cast<uint32_t>(online);
  }
  return std::nullopt;
}

}  // namespace datadog::platform
