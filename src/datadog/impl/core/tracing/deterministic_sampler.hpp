// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <memory>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/tracing/sampler.hpp"

namespace datadog::impl {

/**
 * Deterministic sampler that makes consistent sampling decisions for a given seed.
 *
 * This sampler uses Knuth hashing to compute a uniform hash of the seed, allowing
 * deterministic sampling based on a given sample rate.
 */
class DeterministicSampler final : public ISampler {
 public:
  /**
   * Creates a new sampler with the given seed and sample rate.
   *
   * @param seed The seed for this sampler.
   * @param sample_rate A sample rate between 0 and 100.
   */
  DeterministicSampler(uint64_t seed, float sample_rate);

  /**
   * Creates a new sampler for a given session ID. The sampler seed is derived
   * from the given UUID.
   *
   * @param session_id A UUID representing a session ID.
   * @param sample_rate A sample rate between 0 and 100.
   */
  DeterministicSampler(const UUID& session_id, float sample_rate);

  bool Sample() override;

  float GetRulePSR() override;

  std::unique_ptr<ISampler> CombineWith(float child_rate) override;

 private:
  /**
   * Sampler seed.
   *
   * The value is kept so a new combined sampler can be generated based on the
   * same seed.
   */
  uint64_t _seed;

  /** The sample rate, between 0 and 100. */
  float _sample_rate;

  /**
   * Sampling decision.
   *
   * Since the decision is deterministic for a given seed and sample rate,
   * it's cached in this class member.
   */
  bool _is_sampled;
};

}  // namespace datadog::impl
