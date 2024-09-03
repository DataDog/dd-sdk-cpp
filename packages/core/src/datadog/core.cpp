// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "core.h"

namespace datadog::core {

using datadog::core::internal::Writer;

DatadogCoreContext::DatadogCoreContext(const DatadogCoreConfiguration& config)
    : application_name_(config.application_name),
      application_version_(config.application_version) {}

DatadogCore::DatadogCore(IDatadogCore::Allow allow,
                         const DatadogCoreConfiguration& config)
    : time_provider_(config.time_provider), context_(config) {}

void DatadogCore::Write(FeatureId feature,
                        IDatadogCore::CoreWriteCallback write_callback) const {
  // TODO: Get feature storage, create a writer for feature storage
  Writer writer;
  write_callback(context_, &writer);
}

void DatadogCore::RegisterFeature(FeatureId feature_id,
                                  std::unique_ptr<DatadogFeature> feature) {
  // TODO: Create storage for the feature
  features_by_id_.emplace(feature_id, std::move(feature));
}

DatadogFeature* DatadogCore::FindFeatureById(FeatureId feature_id) const {
  if (auto it = features_by_id_.find(feature_id); it != features_by_id_.end()) {
    return it->second.get();
  }

  return nullptr;
}

}  // namespace datadog::core
