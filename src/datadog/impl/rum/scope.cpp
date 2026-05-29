// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scope.hpp"

#include <array>
#include <cstdint>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

namespace datadog::impl {

RumScopeDependencies::RumScopeDependencies(
    const RumConfig& config, const platform::IClock& in_clock
)
    : application_id(config.application_id),
      clock(in_clock),
      _sampling_rate(config.session_sample_rate) {
  _encode_buffer.reserve(8192);
}

namespace {

// Decimal 1_111_111_111_111_111_111 — the Knuth multiplier shared by the iOS and
// Android SDKs. NOT hex 0xFFFF...; the decimal "all 1s" form is load-bearing for
// cross-SDK consistency.
constexpr uint64_t kSamplerHasher = 0x0F6B75AB2BC471C7ULL;

// 2^64 − 1: the unscaled upper bound for comparing the hash result.
constexpr uint64_t kMaxId = 0xFFFFFFFFFFFFFFFFULL;

// Extract the 48-bit RFC 4122 "node" field (bytes 10–15) as a uint64. Byte-by-byte
// assembly is endian-independent on the host.
uint64_t extract_seed(const std::array<uint8_t, 16>& uuid_bytes) {
  uint64_t seed = 0;
  for (size_t i = 10; i < 16; ++i) {
    seed = (seed << 8) | static_cast<uint64_t>(uuid_bytes.at(i));
  }
  return seed;
}

}  // namespace

bool ShouldSampleSessionFromSeed(uint64_t seed, float sample_rate) {
  if (sample_rate >= 100.0f) {
    return true;
  }
  if (sample_rate <= 0.0f) {
    return false;
  }

  // Unsigned overflow wraps modulo 2^64 — well-defined in C++, and matches Swift's
  // `&*` and Kotlin's ULong multiply exactly.
  const uint64_t hash = seed * kSamplerHasher;

  // Compute threshold in double, in the same order as the mobile SDKs:
  // MAX_ID * rate / 100.0, not MAX_ID * (rate / 100.0) — the two differ in double.
  const double threshold =
      static_cast<double>(kMaxId) * static_cast<double>(sample_rate) / 100.0;

  return static_cast<double>(hash) < threshold;
}

bool RumScopeDependencies::ShouldSampleSession(const UUID& session_id) const {
  return ShouldSampleSessionFromSeed(extract_seed(session_id.bytes), _sampling_rate);
}

}  // namespace datadog::impl
