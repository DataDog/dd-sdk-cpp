// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "time_provider.h"

#include <chrono>

namespace datadog::core {

uint64_t DefaultDateTimeProvider() {
  using UnsignedNanoseconds = std::chrono::duration<uint64_t, std::nano>;
  using SystemNanoseconds =
      std::chrono::time_point<std::chrono::system_clock, UnsignedNanoseconds>;
  SystemNanoseconds now = std::chrono::system_clock::now();
  return now.time_since_epoch().count();
}

}  // namespace datadog::core
