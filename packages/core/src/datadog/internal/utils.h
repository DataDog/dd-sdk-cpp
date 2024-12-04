// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <cstdint>

#include "datadog/feature.h"

namespace datadog::core::internal {

using datadog::core::FeatureId;

constexpr FeatureId CreateFourCC(unsigned char a,
                                 unsigned char b,
                                 unsigned char c,
                                 unsigned char d) {
  return FeatureId{
      static_cast<uint32_t>(a) << 0 | static_cast<uint32_t>(b) << 8 |
      static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24};
}

constexpr uint64_t NanoToMillis(uint64_t nano) {
  return nano / 1'000'000;
}

template <typename T>
struct no_default {
  // Over-explicitly showing that it isn't default constructable.
  no_default() = delete;
  no_default(T&& t) : value{std::move(t)} {}
  no_default(const T& t) : value{t} {}
  T value;
};

}  // namespace datadog::core::internal
