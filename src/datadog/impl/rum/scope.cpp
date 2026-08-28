// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scope.hpp"

#include <array>
#include <cstdint>
#include <limits>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

namespace datadog::impl {

// Extract the 48-bit RFC 4122 "node" field (bytes 10–15) as a uint64. Byte-by-byte
// assembly is endian-independent on the host.
static uint64_t extract_seed(const std::array<uint8_t, 16>& uuid_bytes) {
  uint64_t seed = 0;
  for (size_t i = 10; i < 16; ++i) {
    seed = (seed << 8) | static_cast<uint64_t>(uuid_bytes.at(i));
  }
  return seed;
}

bool ShouldSampleSessionFromSeed(uint64_t seed, float sample_rate) {
  // If sampling rate is 100%, all sessions are sampled
  if (sample_rate >= 100.0f) {
    return true;
  }

  // If sampling rate is 0%, all sessions are ignored
  if (sample_rate <= 0.0f) {
    return false;
  }

  // Session may or may not be sampled, depending on the sample rate and the lower
  // 48 bits of the session ID (the seed). This is a deterministic algorithm that must
  // be implemented identically across all Datadog SDKs, regardless of language or
  // runtime.

  // Multiply by our Knuth factor, effectively spreading out the 48-bit input value
  // across the range of [0..0xFFFFFFFFFFFFFFFF] in a uniform, predictable way. Unsigned
  // overflow wraps at 2^64, which is well-defined in C++ and matches the iOS and
  // Android SDKs exactly (`&*` in Swift; ULong multiply in Kotlin)
  static const uint64_t knuth_factor = 1111111111111111111ull;
  const uint64_t hash = seed * knuth_factor;

  // Using double-precision floating-point arithmetic, map our sample rate to the same
  // [0..0xFFFFFFFFFFFFFFFF] range. Note that order matters here: `max * (rate / 100.0)`
  // would be mathematically the same, but would produce subtly different results due to
  // intermediate floating-point rounding error.
  const double max_id_f64 = static_cast<double>(std::numeric_limits<uint64_t>::max());
  const double sample_rate_f64 = static_cast<double>(sample_rate);
  const double threshold = max_id_f64 * sample_rate_f64 / 100.0;

  // Compare our hash result to the threshold: if we landed under the sample rate, we'll
  // track the session (e.g. at 80% => 80% below sampled, 20% above dropped)
  return static_cast<double>(hash) < threshold;
}

RumScopeDependencies::RumScopeDependencies(
    const RumConfig& config, const platform::IClock& in_clock
)
    : application_id(config.application_id),
      clock(in_clock),
      track_anonymous_user(config.track_anonymous_user),
      _sampling_rate(config.session_sample_rate) {
  _encode_buffer.reserve(8192);
}

bool RumScopeDependencies::ShouldSampleSession(
    const UUID& session_id, std::optional<float> sample_rate
) const {
  // Extract the lower 48 bits from the UUID as the input value for our
  // Knuth-multiplicative-hashing algorithm, then compare the resulting hash value
  // against a threshold derived from our configured sampling rate
  const uint64_t seed = extract_seed(session_id.bytes);
  return ShouldSampleSessionFromSeed(seed, sample_rate.value_or(_sampling_rate));
}

}  // namespace datadog::impl
