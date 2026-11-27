// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <chrono>
#include <cstdint>
#include <optional>
#include <ratio>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/platform/cpu_info_provider.hpp"
#include "datadog/impl/types/windows_headers.hpp"

namespace datadog::platform {

namespace {

// FILETIME durations count 100-nanosecond intervals
using FileTimeDuration = std::chrono::duration<uint64_t, std::ratio<1, 10'000'000>>;

Duration FileTimeToDuration(const FILETIME& value) {
  const uint64_t ticks = (static_cast<uint64_t>(value.dwHighDateTime) << 32U) |
                         static_cast<uint64_t>(value.dwLowDateTime);
  return std::chrono::duration_cast<Duration>(FileTimeDuration(ticks));
}

}  // namespace

// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes
std::optional<Duration> CpuInfoProvider::CpuTime() const {
  // Accounted at the scheduler tick (~15.6 ms), which is precise enough for 1 s samples
  FILETIME creation_time{};
  FILETIME exit_time{};
  FILETIME kernel_time{};
  FILETIME user_time{};
  const BOOL read = GetProcessTimes(
      GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time
  );
  if (read == 0) {
    return std::nullopt;
  }
  return FileTimeToDuration(kernel_time) + FileTimeToDuration(user_time);
}

// https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getactiveprocessorcount
std::optional<uint32_t> CpuInfoProvider::LogicalCpuCount() const {
  // Counts every processor group, so machines with more than 64 CPUs are covered
  const DWORD count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  if (count == 0) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(count);
}

}  // namespace datadog::platform
