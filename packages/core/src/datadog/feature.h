// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.

#pragma once

#include <string>

#include "datadog/core.h"

namespace datadog::core {

enum class FeatureId : uint32_t {};

class DatadogFeature {
 public:
  virtual ~DatadogFeature() = default;

  virtual FeatureId GetFeatureId() = 0;

  virtual void ContextChanged(
      const std::unordered_map<std::string, std::string>& changed_context) = 0;
};

}  // namespace datadog::core
