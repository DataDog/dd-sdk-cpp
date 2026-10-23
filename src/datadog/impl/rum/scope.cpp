// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/scope.hpp"

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

namespace datadog::impl {

RumScopeDependencies::RumScopeDependencies(
    const RumConfig& config, const platform::IClock& in_clock
)
    : application_id(config.application_id),
      clock(in_clock),
      track_anonymous_user(config.track_anonymous_user),
      context_receiver(config.context_receiver),
      _sampling_rate(config.session_sample_rate) {
  _encode_buffer.reserve(8192);
}

std::unique_ptr<DeterministicSampler> RumScopeDependencies::MakeSampler(
    const UUID& session_id
) const {
  return std::make_unique<DeterministicSampler>(session_id, _sampling_rate);
}

}  // namespace datadog::impl
