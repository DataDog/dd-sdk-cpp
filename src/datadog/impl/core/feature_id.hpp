// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>

namespace datadog::impl {

/**
 * Globally unique FourCC code that identifies a specific feature.
 */
using FeatureId = uint32_t;

/**
 * Encodes the given four characters into a uint32 in FourCC format, with the leftmost
 * character occupying the least significant byte of the resulting value. e.g.
 * CreateFeatureId("ABCD") => 0x44434241
 */
constexpr FeatureId CreateFeatureId(const char fourcc[5]) {
  const uint32_t a = static_cast<uint32_t>(fourcc[0]) << 0;
  const uint32_t b = static_cast<uint32_t>(fourcc[1]) << 8;
  const uint32_t c = static_cast<uint32_t>(fourcc[2]) << 16;
  const uint32_t d = static_cast<uint32_t>(fourcc[3]) << 24;
  return a | b | c | d;
}

}  // namespace datadog::impl
