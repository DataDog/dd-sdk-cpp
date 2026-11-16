// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "span_id.hpp"

#include <array>
#include <cstdio>

namespace datadog::impl {

SpanID::SpanID(uint64_t value) : _value(value) {}

std::string SpanID::ToDecimalString() const { return std::to_string(_value); }

std::string SpanID::ToHexString() const {
  // 16 hexadecimal characters plus the null terminator.
  std::array<char, 17> buffer{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  std::snprintf(buffer.data(), buffer.size(), "%016" PRIx64, _value);
  return std::string(buffer.data(), buffer.size() - 1);
}

}  // namespace datadog::impl
