// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "trace_id_generator.hpp"

#include <chrono>
#include <cstdint>

namespace datadog::impl {

DefaultTraceIDGenerator::DefaultTraceIDGenerator(const platform::IClock& clock)
    : _clock(clock), _random_generator(1) {}

TraceID DefaultTraceIDGenerator::Generate() {
  const auto now_in_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(_clock.Now().time_since_epoch());
  const auto unix_seconds = now_in_seconds.count();

  const uint64_t high = static_cast<uint64_t>(unix_seconds) << 32;

  return TraceID(high, _random_generator.NextValue());
}

}  // namespace datadog::impl
