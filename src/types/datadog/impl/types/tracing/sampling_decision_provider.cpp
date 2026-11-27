// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/tracing/sampling_decision_provider.hpp"

#include <mutex>

namespace datadog::impl {

void SamplingDecisionProvider::SetSessionSnapshot(
    const UUID& session_id, const DeterministicSampler& sampler
) {
  std::lock_guard lock(_session_snapshot_mutex);
  _session_snapshot = SessionSnapshot{session_id, sampler};
}

std::optional<SamplingDecisionProvider::SessionSnapshot>
SamplingDecisionProvider::GetSessionSnapshot() const {
  std::lock_guard lock(_session_snapshot_mutex);
  return _session_snapshot;
}

}  // namespace datadog::impl
