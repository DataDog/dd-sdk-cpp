// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/internal/core_internal.h"

#include "datadog/internal/owning_blocking_queue.h"
#include "datadog/internal/performance_preset.h"
#include "datadog/internal/reporting_thread.h"
#include "datadog/internal/sdk_version.h"
#include "datadog/storage/datadog_file_system.h"
#include "datadog/storage/feature_storage.h"

namespace datadog::core::internal {

std::string_view HostFromSite(Site site) {
  switch (site) {
    case Site::us1:
      return "https://browser-intake-datadoghq.com/";
    case Site::us3:
      return "https://browser-intake-us3-datadoghq.com/";
    case Site::us5:
      return "https://browser-intake-us5-datadoghq.com/";
    case Site::eu1:
      return "https://browser-intake-datadoghq.eu/";
    case Site::ap1:
      return "https://browser-intake-ap1-datadoghq.com/";
    case Site::us1_fed:
      return "https://browser-intake-ddog-gov.com/";
    default:
      return "";
  }
}

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
        configuration_(configuration),
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
    return feature_info_by_id_.find(feature_id) != feature_info_by_id_.end();
  };

  std::shared_ptr<DatadogFeature> GetFeatureById(
      FeatureId feature_id) const override {
    if (const auto& it = feature_info_by_id_.find(feature_id);
        it != feature_info_by_id_.end()) {
      return it->second.feature;
    }

    return nullptr;
  }

  void Start() override {
    if (!is_started_) {
      CreateReporter();
      auto shared_this =
          std::dynamic_pointer_cast<DatadogCoreImpl>(shared_from_this());

      storage_thread_ = std::thread(StorageThreadStart, shared_this);
      reporting_thread_ =
          std::make_unique<ReportingThread>(shared_this, performance_preset_);
      reporting_thread_->Start();
    }
    is_started_ = true;
  }

  bool IsRunning() const override { return is_started_; }

  void Shutdown() override {
    // Graceful shutdown, try to clear out the storage queue before a full
    // shutdown
    if (is_started_) {
      // Thread shutdown is a sync operation and automatically joins the
      // encapsulated thread.
      reporting_thread_->Shutdown();

      storage_queue_.Shutdown();
      storage_thread_.join();
    }
  }

  const FeatureInfoMap& GetFeatureInfoMap() const override {
    return feature_info_by_id_;
  }
  void CreateReporter() override {
    if (!reporter_) {
      auto host_url = HostFromSite(configuration_.datadog_site.value);
      reporter_ = configuration_.reporter_create_func(host_url);
      if (!reporter_) {
        // TELEM: Add telemetry. Start failed.
        // Should throw?
      }
    }
  }
  std::shared_ptr<reporting::DatadogReporter> GetReporter() const override {
    return reporter_;
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
    FeatureInfo feature_info{
        feature_id,
        feature,
        std::make_unique<FeatureStorage>(std::string{feature->GetName()},
                                         performance_preset_, time_provider_,
                                         feature_file_system),
    };
    feature_info_by_id_.emplace(feature_id, std::move(feature_info));
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
        auto& feature_storage =
            feature_info_by_id_[opt_val->feature_id()].storage;
        feature_storage->Write(opt_val->data());
      } else {
        should_continue = !storage_queue_.IsEmpty();
      }
    }
  }

  bool is_started_;

  // Should we actually save the configuraiton?
  DatadogConfiguration configuration_;
  internal::PerformancePreset performance_preset_;
  internal::CoreContext core_context_;

  // TODO(jeff.ward): Encapsulate into a separate class?
  std::thread storage_thread_;
  internal::OwningBlockingQueue<CoreMessage> storage_queue_;

  std::unique_ptr<ReportingThread> reporting_thread_;

  std::shared_ptr<reporting::DatadogReporter> reporter_;

  std::shared_ptr<storage::DatadogFileSystem> file_system_;
  DateTimeProvider time_provider_;

  FeatureInfoMap feature_info_by_id_;
};

std::shared_ptr<DatadogCoreInternal> DatadogCoreInternal::Create(
    const DatadogConfiguration& configuration) {
  auto core =
      std::make_shared<DatadogCoreImpl>(DatadogCore::Allow{}, configuration);
  return core;
}

}  // namespace datadog::core::internal
