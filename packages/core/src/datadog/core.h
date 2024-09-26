// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <map>
#include <memory>
#include <type_traits>

#include "datadog/feature.h"
#include "datadog/storage/datadog_file_system.h"

namespace datadog::core {

class DatadogConfiguration {
 public:
  explicit DatadogConfiguration(
      const std::shared_ptr<storage::DatadogFileSystem>& file_system =
          std::make_shared<storage::StdDatadogFileSystem>())
      : file_system_{file_system} {};

  const std::shared_ptr<storage::DatadogFileSystem>& GetFileSystem() const {
    return file_system_;
  }

 private:
  std::shared_ptr<storage::DatadogFileSystem> file_system_;
};

class IDatadogCore {
 protected:
  enum class Allow { ctor };

 public:
  virtual ~IDatadogCore() = default;
};

class DatadogCore : public IDatadogCore,
                    std::enable_shared_from_this<DatadogCore> {
 public:
  explicit DatadogCore(IDatadogCore::Allow,
                       const DatadogConfiguration& configuration)
      : file_system_{configuration.GetFileSystem()} {};
  DatadogCore(const DatadogCore&) = delete;
  DatadogCore& operator=(const DatadogCore&) = delete;

  template <typename T, typename... Args>
  void RegisterFeature(Args&&... args);

  template <typename T>
  T* GetFeature() const {
    auto feature_opt = GetFeatureById(T::feature_id);
    return dynamic_cast<T*>(feature_opt);
  }

  static auto Create(const DatadogConfiguration& configuration) {
    return std::make_shared<DatadogCore>(IDatadogCore::Allow::ctor,
                                         configuration);
  }

 private:
  void RegisterFeature(FeatureId feature_id,
                       std::unique_ptr<DatadogFeature>&& feature);
  DatadogFeature* GetFeatureById(FeatureId feature_id) const;

  std::shared_ptr<storage::DatadogFileSystem> file_system_;
  std::map<FeatureId, std::unique_ptr<DatadogFeature>> features_by_id_;
};

template <typename T, typename... Args>
void DatadogCore::RegisterFeature(Args&&... args) {
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
    return;
  }

  auto feature = std::make_unique<T>(weak_from_this(), std::forward(args)...);
  RegisterFeature(T::feature_id, std::move(feature));
}

}  // namespace datadog::core
