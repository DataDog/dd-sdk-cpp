// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <sys/sysctl.h>
#include <time.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/platform/cpu_info_provider.hpp"

namespace datadog::platform {

// See https://github.com/apple-oss-distributions/Libc/blob/main/gen/clock_gettime.c
// (CLOCK_PROCESS_CPUTIME_ID is getrusage(RUSAGE_SELF): user + system time, in us)
std::optional<Duration> CpuInfoProvider::CpuTime() const {
  // Covers every thread of the process, exited ones included (macOS 10.12+)
  timespec ts{};
  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) {
    return std::nullopt;
  }
  // A timespec is whole seconds plus the remaining nanoseconds (0 to 999'999'999)
  return std::chrono::seconds(ts.tv_sec) + std::chrono::nanoseconds(ts.tv_nsec);
}

// https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_system_capabilities
// https://developer.apple.com/documentation/foundation/processinfo/activeprocessorcount
std::optional<uint32_t> CpuInfoProvider::LogicalCpuCount() const {
  // Enabled logical cores, performance and efficiency alike. An alias of
  // hw.logicalcpu; ProcessInfo.activeProcessorCount (used by the iOS SDK) reads it too
  int count = 0;
  size_t size = sizeof(count);
  if (sysctlbyname("hw.activecpu", &count, &size, nullptr, 0) != 0 || count <= 0) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(count);
}

}  // namespace datadog::platform
