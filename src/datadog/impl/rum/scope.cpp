// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scope.hpp"

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/types/sampling.hpp"

namespace datadog::impl {

RumScopeDependencies::RumScopeDependencies(
    const RumConfig& config, const platform::IClock& in_clock
)
    : application_id(config.application_id),
      clock(in_clock),
      track_anonymous_user(config.track_anonymous_user),
      _sampling_rate(config.session_sample_rate) {
  _encode_buffer.reserve(8192);
}

bool RumScopeDependencies::ShouldSampleSession(const UUID& session_id) const {
  // Extract the lower 48 bits from the UUID as the input value for our
  // Knuth-multiplicative-hashing algorithm, then compare the resulting hash value
  // against a threshold derived from our configured sampling rate
  const uint64_t seed = ExtractSamplingSeed(session_id);
  return ShouldSample_Deterministic(seed, _sampling_rate);
}

}  // namespace datadog::impl
