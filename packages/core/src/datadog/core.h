// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <memory>
#include <type_traits>
#include <unordered_map>

#include "datadog/feature.h"

namespace datadog::core {

constexpr FeatureId four_cc(unsigned char a,
                            unsigned char b,
                            unsigned char c,
                            unsigned char d) {
  return FeatureId{
      static_cast<uint32_t>(a) << 0 | static_cast<uint32_t>(b) << 8 |
      static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24};
}

class IDatadogCore {
 protected:
  enum class Allow { ctor };

 public:
  virtual ~IDatadogCore() = default;
};

class DatadogCore : public IDatadogCore,
                    std::enable_shared_from_this<DatadogCore> {
 public:
  explicit DatadogCore(IDatadogCore::Allow allow){};
  DatadogCore(const DatadogCore&) = delete;
  DatadogCore& operator=(const DatadogCore&) = delete;

  template <typename T>
  void RegisterFeature();

  template <typename T>
  T* GetFeature() const {
    auto feature_opt = GetFeatureById(T::feature_id);
    return dynamic_cast<T*>(feature_opt);
  }

  static std::shared_ptr<DatadogCore> Create() {
    return std::make_shared<DatadogCore>(IDatadogCore::Allow::ctor);
  }

 private:
  void RegisterFeature(FeatureId feature_id,
                       std::unique_ptr<DatadogFeature> feature);
  DatadogFeature* GetFeatureById(FeatureId feature_id) const;

  std::unordered_map<FeatureId, std::unique_ptr<DatadogFeature>>
      features_by_id_;
};

template <typename T>
void DatadogCore::RegisterFeature() {
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

  auto feature = std::make_unique<T>(weak_from_this());
  RegisterFeature(T::feature_id, std::move(feature));
}

}  // namespace datadog::core
