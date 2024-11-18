// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <memory>

#include "datadog/storage/feature_storage.h"

namespace datadog::core::internal {

// Feature info holds all information about a specific feature
// to make exchanging that information around the Core easier.
struct FeatureInfo {
  using FeatureStorage = datadog::core::storage::FeatureStorage;

  FeatureId feature_id;
  std::shared_ptr<DatadogFeature> feature;
  std::unique_ptr<FeatureStorage> storage;
};

}  // namespace datadog::core::internal
