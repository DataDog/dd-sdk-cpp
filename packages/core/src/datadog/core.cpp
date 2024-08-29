// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "core.h"

namespace datadog::core {

using datadog::core::internal::Writer;

DatadogCoreContext::DatadogCoreContext(const DatadogCoreConfiguration& config)
    : application_name_(config.application_name),
      time_provider_(config.time_provider) {
  if (time_provider_ == nullptr) {
    time_provider_ = DefaultTimeProvider;
  }
}

DatadogCore::DatadogCore(const DatadogCore::CtorKey&,
                         const DatadogCoreConfiguration& config)
    : context_(config) {}

void DatadogCore::Write(FeatureId feature,
                        std::function<void(const DatadogCoreContext&,
                                           datadog::core::internal::Writer*)>
                            write_callback) const {
  // TODO: Get feature storage, create a writer for feature storage
  Writer writer;
  write_callback(context_, &writer);
}

void DatadogCore::RegisterFeature(FeatureId feature_id,
                                  std::unique_ptr<DatadogFeature> feature) {
  // TODO: Create storage for the feature
  features_.emplace(feature_id, std::move(feature));
}

DatadogFeature* DatadogCore::GetFeatureById(FeatureId feature_id) const {
  auto feature_itr = features_.find(feature_id);
  if (feature_itr != features_.end()) {
    return feature_itr->second.get();
  }

  return nullptr;
}

}  // namespace datadog::core
