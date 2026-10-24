// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

namespace datadog::impl {

/**
 * Interface for a sampler.
 *
 * Samples may be combined with a new sample rate to obtain a new sampler with
 * the combined sample rate.
 */
class ISampler {
 public:
  virtual ~ISampler() = default;

  /** `true` if this sampler made a positive decision on sampling, `false` otherwise. */
  virtual bool Sample() = 0;

  /**
   * The value to be used in the `rule_psr` key given this sampler configuration.
   *
   * @return A value between 0 and 1.
   */
  virtual float GetRulePSR() = 0;

  /**
   * Obtains a new sampler with the combined sample rate between this sampler's original
   * sample rate and the given child sample rate.
   *
   * Although implementations may do as they wish, it's usually expected the returned
   * sampler has a sample rate equal to the normalized product of the original and child
   * sample rates.
   *
   * @param child_rate A child sample rate between 0 and 100 to combine with this
   * sampler's rate.
   */
  virtual std::unique_ptr<ISampler> CombineWith(float child_rate) = 0;
};

}  // namespace datadog::impl