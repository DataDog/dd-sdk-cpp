// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/api.hpp"

namespace datadog {

// Forward declarations
namespace impl {
class Rum;
}  // namespace impl

/**
 * Configures the details of the RUM feature upon initialization.
 */
struct DATADOG_API RumConfig {
  friend class Rum;
  friend class impl::Rum;

 public:
  RumConfig() = default;
};

/**
 * Interface to the Datadog SDK's RUM feature.
 */
class DATADOG_API Rum {
 public:
  /**
   * Registers the RUM feature with the core of the Datadog SDK.
   */
  static std::shared_ptr<Rum> Register(
      class Core& core, const RumConfig& config = RumConfig()
  );

 private:
  std::shared_ptr<impl::Rum> _impl;
};

}  // namespace datadog
