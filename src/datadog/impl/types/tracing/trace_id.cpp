// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "trace_id.hpp"

#include <array>
#include <cstdio>

namespace datadog::impl {

TraceID::TraceID(uint64_t high, uint64_t low) : _high(high), _low(low) {}

std::string TraceID::ToLowDecimalString() const { return std::to_string(_low); }

std::string TraceID::ToHexString() const {
  // 32 hexadecimal characters plus the null terminator.
  std::array<char, 33> buffer{};

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  std::snprintf(buffer.data(), buffer.size(), "%016" PRIx64 "%016" PRIx64, _high, _low);

  return std::string(buffer.data(), buffer.size() - 1);
}

}  // namespace datadog::impl
