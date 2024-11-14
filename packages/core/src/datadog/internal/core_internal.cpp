// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/internal/core_internal.h"

#include "datadog/internal/owning_blocking_queue.h"
#include "datadog/internal/performance_preset.h"
#include "datadog/internal/sdk_version.h"
#include "datadog/storage/datadog_file_system.h"
#include "datadog/storage/feature_storage.h"

namespace datadog::core::internal {

using datadog::core::CoreMessage;
using datadog::core::storage::DatadogFileSystem;
using datadog::core::storage::FeatureStorage;

// Full implementation of the DatadogCore
class DatadogCoreImpl : public DatadogCoreInternal {
 public:
  explicit DatadogCoreImpl(DatadogCore::Allow allow,
                           const DatadogConfiguration& configuration)
      : DatadogCoreInternal(allow),
        is_started_(false),
        performance_preset_{configuration.batch_size,
                            configuration.upload_frequency,
                            configuration.batch_processing_level},
        core_context_{std::string(configuration.service.value),
                      std::string(configuration.env.value), kSdkVersion},
        file_system_{configuration.file_system},
        time_provider_{configuration.date_time_provider} {}

  ~DatadogCoreImpl() override = default;
  DatadogCoreImpl(DatadogCoreImpl&&) = delete;
  DatadogCoreImpl& operator=(DatadogCoreImpl&&) = delete;
  DatadogCoreImpl(const DatadogCoreImpl&) = delete;
  DatadogCoreImpl& operator=(const DatadogCoreImpl&) = delete;

  Nanoseconds GetNow() const override { return Nanoseconds(time_provider_()); }
  const internal::CoreContext& GetContext() const override {
    return core_context_;
  }

  void SendMessage(CoreMessage&& message) override {
    // TODO(jeff.ward): Inform features of context changes.
    storage_queue_.Push(std::move(message));
  }

  bool FeatureExists(FeatureId feature_id) const override {
    return features_by_id_.find(feature_id) != features_by_id_.end();
  };

  std::shared_ptr<DatadogFeature> GetFeatureById(
      FeatureId feature_id) const override {
    if (auto it = features_by_id_.find(feature_id);
        it != features_by_id_.end()) {
      return it->second;
    }

    return nullptr;
  }

  void Start() {
    if (!is_started_) {
      auto shared_this =
          std::dynamic_pointer_cast<DatadogCoreImpl>(shared_from_this());
      storage_thread_ = std::thread(StorageThreadStart, shared_this);
    }
    is_started_ = true;
  }

  void Shutdown() override {
    // Graceful shutdown, try to clear out the storage queue before a full
    // shutdown
    auto was_started = std::exchange(is_started_, false);
    if (was_started) {
      storage_queue_.Shutdown();
      storage_thread_.join();
    }
  }

 private:
  using FeatureStorage = datadog::core::storage::FeatureStorage;

  void RegisterFeature(
      FeatureId feature_id,
      const std::shared_ptr<DatadogFeature>& feature) override {
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

  // This must be passed by value as it's going over a thread barrier
  // NOLINTNEXTLINE(performance-unnecessary-value-param)
  static void StorageThreadStart(std::shared_ptr<DatadogCoreImpl> self) {
    self->StorageThreadProc();
  }
  void StorageThreadProc() {
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

  bool is_started_;

  internal::PerformancePreset performance_preset_;
  internal::CoreContext core_context_;

  // TODO(jeff.ward): Encapsulate into a separate class?
  std::thread storage_thread_;
  internal::OwningBlockingQueue<CoreMessage> storage_queue_;

  std::shared_ptr<storage::DatadogFileSystem> file_system_;
  DateTimeProvider time_provider_;

  std::map<FeatureId, std::shared_ptr<DatadogFeature>> features_by_id_;
  std::map<FeatureId, std::unique_ptr<FeatureStorage>> storage_by_feature_id_;
};

std::shared_ptr<DatadogCoreInternal> DatadogCoreInternal::Create(
    const DatadogConfiguration& configuration) {
  auto core =
      std::make_shared<DatadogCoreImpl>(DatadogCore::Allow{}, configuration);
  core->Start();
  return core;
}

}  // namespace datadog::core::internal
