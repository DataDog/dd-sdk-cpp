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

void DatadogCore::SendMesage(FeatureId feature, const CoreMessage& message) {
  if (!message.GetData().empty()) {
    // Queue the write to the feature, processed by the Core thread. This would
    // need to be modified to ensure thread safety. The repeated copy
    // of std::string plus the overhead of the queue are potential downsides to
    // this approach.
    //
    // We also run the risk of growing this queue without having enough time to
    // process what's in it, especially when all features use a single write
    // queue.
    write_queue_.push_back({feature, message.GetData()});
  }

  const auto& context_changes = message.GetContextChanges();
  if (!context_changes.empty()) {
    for (const auto& feature_it : features_by_id_) {
      if (feature_it.second->GetFeatureId() != feature) {
        feature_it.second->ContextChanged(context_changes);
      }
    }
  }
}

void DatadogCore::ProcessWrites() {
  // Sepearate thread performs the writes to storage / caching. This is using
  // std::list to show the concept, but a real solution would use a structure
  // allowing a clean producer / consumer pattern
  while (!write_queue_.empty()) {
    auto write = write_queue_.front();
    write_queue_.pop_front();

    // Storage is created for each feature, and writer is created from storage
    // (this is why the feature id is held with each write).
    Writer w;
    w.Write(write.second);
  }
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
