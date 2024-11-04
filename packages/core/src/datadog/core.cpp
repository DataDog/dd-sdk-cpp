// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "core.h"

#include "datadog/internal/sdk_version.h"

namespace datadog::core {

using datadog::core::internal::kSdkVersion;
using datadog::core::storage::DatadogFileSystem;

DatadogCore::DatadogCore(IDatadogCore::Allow,
                         const DatadogConfiguration& configuration)
    : is_started_(false),
      performance_preset_{configuration.batch_size,
                          configuration.upload_frequency,
                          configuration.batch_processing_level},
      core_context_{std::string(configuration.service.value),
                    std::string(configuration.env.value), kSdkVersion},
      file_system_{configuration.file_system},
      time_provider_{configuration.date_time_provider} {};

void DatadogCore::Start() {
  if (!is_started_) {
    storage_thread_ = std::thread(StorageThreadStart, shared_from_this());
  }
  is_started_ = true;
}

void DatadogCore::Shutdown() {
  // Graceful shutdown, try to clear out the storage queue before a full
  // shutdown
  auto was_started = std::exchange(is_started_, false);
  if (was_started) {
    storage_queue_.Shutdown();
    storage_thread_.join();
  }
}

void DatadogCore::RegisterFeature(
    FeatureId feature_id,
    const std::shared_ptr<DatadogFeature>& feature) {
  // The public version of this function already checked that the feature
  // doesn't exist, should be safe to just emplace in this function.
  std::shared_ptr<DatadogFileSystem> feature_file_system{
      file_system_->CreateChildFileSystem(feature->GetName())};
  storage_by_feature_id_.emplace(
      feature_id, std::make_unique<FeatureStorage>(
                      std::string{feature->GetName()}, performance_preset_,
                      time_provider_, feature_file_system));

  features_by_id_.emplace(feature_id, feature);
}

std::shared_ptr<DatadogFeature> DatadogCore::GetFeatureById(
    FeatureId feature_id) const {
  if (auto it = features_by_id_.find(feature_id); it != features_by_id_.end()) {
    return it->second;
  }

  return nullptr;
}

void DatadogCore::SendMessage(CoreMessage&& message) {
  // TODO(jeff.ward): Inform features of context changes.
  storage_queue_.Push(std::move(message));
}

void DatadogCore::StorageThreadProc() {
  bool should_continue = true;
  while (should_continue) {
    auto opt_val = storage_queue_.GetNext();
    if (opt_val.has_value()) {
      auto& feature_storage = storage_by_feature_id_[opt_val->feature_id()];
      feature_storage->Write(opt_val->data());
    } else {
      should_continue = !storage_queue_.IsEmpty();
    }
  }
}

}  // namespace datadog::core
