// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/sampling.hpp"

#include <limits>

namespace datadog::impl {

uint64_t ExtractSamplingSeed(const UUID& uuid) {
  uint64_t seed = 0;
  for (size_t i = 10; i < 16; ++i) {
    seed = (seed << 8) | static_cast<uint64_t>(uuid.bytes.at(i));
  }
  return seed;
}

bool ShouldSample_Deterministic(uint64_t seed, float sample_rate) {
  // If sampling rate is 100%, all data is sampled
  if (sample_rate >= 100.0f) {
    return true;
  }

  // If sampling rate is 0%, all data is ignored
  if (sample_rate <= 0.0f) {
    return false;
  }

  // Data may or may not be sampled, depending on the sample rate and the seed, i.e. the
  // lower 48 bits of the associated UUID. This is a deterministic algorithm that must
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
  // sample this data in (e.g. at 80% => 80% below sampled, 20% above dropped)
  return static_cast<double>(hash) < threshold;
}

}  // namespace datadog::impl
