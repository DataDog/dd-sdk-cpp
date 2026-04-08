// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scope.hpp"

#include <string_view>

#include "datadog/rum.hpp"

#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

RumScopeDependencies::RumScopeDependencies(
    const RumConfig& config, const platform::IClock& in_clock
)
    : application_id(config.application_id),
      clock(in_clock),
      _sampling_rate_unit(config.session_sample_rate / 100.0f),
      _sampling_rng(std::random_device{}()),
      _sampling_distribution(0.0f, 1.0f) {
  _encode_buffer.reserve(8192);
}

bool RumScopeDependencies::ShouldSampleSession() const {
  // If sampling rate is 100%, all sessions are sampled
  if (_sampling_rate_unit >= 1.0f) {
    return true;
  }

  // If sampling rate is 0%, all sessions are ignored
  if (_sampling_rate_unit <= 0.0f) {
    return false;
  }

  // Roll the dice to see if this session should be sampled. At a session sample rate of
  // 0.2f, we keep 20% of sessions, so a roll _under_ the threshold means we should
  // sample this session.
  const float random_unit_value = _sampling_distribution(_sampling_rng);
  return random_unit_value <= _sampling_rate_unit;
}

}  // namespace datadog::impl
