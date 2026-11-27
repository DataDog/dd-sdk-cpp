// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <mutex>
#include <optional>

#include "datadog/uuid.hpp"

#include "datadog/impl/types/tracing/deterministic_sampler.hpp"

namespace datadog::impl {

/**
 * Provides sampling decisions synchronously.
 *
 * Deterministic Sampling introduces dependencies between modules, since both the
 * sampling method and the resulting decision may depend on RUM being enabled and the
 * current Session ID.
 *
 * Additionally, some funcionality requires this information to be available
 * synchronously and cannot wait for context propagation, like HTTP header injection.
 */
class SamplingDecisionProvider {
 public:
  struct SessionSnapshot {
    UUID session_id;
    DeterministicSampler sampler;
  };

  SamplingDecisionProvider() = default;
  ~SamplingDecisionProvider() = default;
  SamplingDecisionProvider(const SamplingDecisionProvider&) = delete;
  SamplingDecisionProvider& operator=(const SamplingDecisionProvider&) = delete;
  SamplingDecisionProvider(SamplingDecisionProvider&&) = delete;
  SamplingDecisionProvider& operator=(SamplingDecisionProvider&&) = delete;

  void SetSessionSnapshot(const UUID& session_id, const DeterministicSampler& sampler);
  std::optional<SessionSnapshot> GetSessionSnapshot() const;

 private:
  mutable std::mutex _session_snapshot_mutex;
  std::optional<SessionSnapshot> _session_snapshot;
};

}  // namespace datadog::impl
