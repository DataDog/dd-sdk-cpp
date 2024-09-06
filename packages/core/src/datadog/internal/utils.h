// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include "datadog/feature.h"

namespace datadog::core::internal {

constexpr FeatureId four_cc(unsigned char a,
                            unsigned char b,
                            unsigned char c,
                            unsigned char d) {
  return FeatureId{
      static_cast<uint32_t>(a) << 0 | static_cast<uint32_t>(b) << 8 |
      static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24};
}

}  // namespace datadog::core::internal
