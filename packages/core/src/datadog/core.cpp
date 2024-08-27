// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "core.h"

namespace datadog::core {

DatadogCore::DatadogCore() {}

void DatadogCore::RegisterFeature(const FeatureId& feature_id,
                                  std::unique_ptr<DatadogFeature> feature) {
  features_.emplace(feature_id, std::move(feature));
}

DatadogFeature* DatadogCore::GetFeatureById(const FeatureId& feature_id) const {
  auto feature_itr = features_.find(feature_id);
  if (feature_itr != features_.end()) {
    return feature_itr->second.get();
  }

  return nullptr;
}

}  // namespace datadog::core
