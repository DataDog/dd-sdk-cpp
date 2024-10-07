// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <map>
#include <memory>
#include <type_traits>

#include "datadog/core_configuration.h"
#include "datadog/core_message.h"
#include "datadog/feature.h"
#include "datadog/internal/core_context.h"
#include "datadog/internal/performance_preset.h"
#include "datadog/storage/datadog_file_system.h"
#include "datadog/storage/feature_storage.h"
#include "datadog/time_provider.h"

namespace datadog::core {

using Nanoseconds = std::chrono::duration<uint64_t, std::nano>;

class IDatadogCore {
 protected:
  enum class Allow { ctor };

 public:
  virtual ~IDatadogCore() = default;

  virtual Nanoseconds GetNow() const = 0;
  // REVISIT: It might be better to pass the initial context during feature
  // initialization, then broadcast changes via the ContextChanged API that
  // we're planning on features
  virtual const internal::CoreContext& GetContext() const = 0;
  virtual void SendMessage(FeatureId feature_id, CoreMessage&& msg) = 0;
};

class DatadogCore : public IDatadogCore,
                    public std::enable_shared_from_this<DatadogCore> {
 public:
  explicit DatadogCore(IDatadogCore::Allow,
                       const DatadogConfiguration& configuration);
  DatadogCore(const DatadogCore&) = delete;
  DatadogCore& operator=(const DatadogCore&) = delete;

  template <typename T, typename... Args>
  T* RegisterFeature(Args&&... args);

  template <typename T>
  T* GetFeature() const {
    auto feature_opt = GetFeatureById(T::feature_id);
    return dynamic_cast<T*>(feature_opt);
  }

  Nanoseconds GetNow() const override { return Nanoseconds(time_provider_()); }
  const internal::CoreContext& GetContext() const override {
    return core_context_;
  }

  void SendMessage(FeatureId feature_id, CoreMessage&& msg) override;

  static auto Create(const DatadogConfiguration& configuration) {
    return std::make_shared<DatadogCore>(IDatadogCore::Allow::ctor,
                                         configuration);
  }

 private:
  using FeatureStorage = datadog::core::storage::FeatureStorage;

  void RegisterFeature(FeatureId feature_id,
                       std::unique_ptr<DatadogFeature>&& feature);
  DatadogFeature* GetFeatureById(FeatureId feature_id) const;

  internal::PerformancePreset performance_preset_;
  internal::CoreContext core_context_;

  std::shared_ptr<storage::DatadogFileSystem> file_system_;
  DateTimeProvider time_provider_;

  std::map<FeatureId, std::unique_ptr<DatadogFeature>> features_by_id_;
  std::map<FeatureId, std::unique_ptr<FeatureStorage>> storage_by_feature_id_;
};

template <typename T, typename... Args>
T* DatadogCore::RegisterFeature(Args&&... args) {
  using TFeatureId = decltype(T::feature_id);
  static_assert(
      std::is_constructible_v<T, std::weak_ptr<IDatadogCore>>,
      "Datadog Feature must be constructable from std::weak_ptr<IDatadogCore>");
  static_assert(
      std::is_same_v<const FeatureId, TFeatureId>,
      "Datadog Feature is missing required const value for feature_id");

  if (const auto& it = features_by_id_.find(T::feature_id);
      it != features_by_id_.end()) {
    // TODO(jeff.ward): Add telemetry / logging that a feature is being
    // registered twice
    return nullptr;
  }

  auto feature = std::make_unique<T>(weak_from_this(), std::forward(args)...);
  auto ptr = feature.get();
  RegisterFeature(T::feature_id, std::move(feature));
  return ptr;
}

}  // namespace datadog::core
