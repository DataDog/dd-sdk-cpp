// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "core.h"

namespace datadog::core {

DatadogConfiguration::DatadogConfiguration(
    const std::shared_ptr<storage::IDatadogFileSystem>& file_system)
    : file_system_{file_system} {}

void DatadogCore::RegisterFeature(FeatureId feature_id,
                                  std::unique_ptr<DatadogFeature>&& feature) {
  // The public version of this function already checked that the feature
  // doesn't exist, should be same to just emplace here
  features_by_id_.emplace(feature_id, std::move(feature));
}

DatadogFeature* DatadogCore::GetFeatureById(FeatureId feature_id) const {
  if (auto it = features_by_id_.find(feature_id); it != features_by_id_.end()) {
    return it->second.get();
  }

  return nullptr;
}

}  // namespace datadog::core
