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
  virtual bool Sample() const = 0;

  /**
   * The value to be used in the `rule_psr` key given this sampler configuration.
   *
   * @return A value between 0 and 1.
   */
  virtual float GetRulePSR() const = 0;
};

}  // namespace datadog::impl
