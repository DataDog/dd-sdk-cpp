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

using datadog::core::FeatureId;

class IDatadogCore {
 public:
  virtual ~IDatadogCore() = default;
};

// DatadogCore is the integration point for individual features that wish to
// send data to Datadog intake. "Features" include Logs, Traces, and Real User
// Monitoring. DatadogCore is responsible for gathering data for individual
// features, caching or storing the data, and sending that data to the proper
// intake endpoint.
class DatadogCore final : public IDatadogCore,
                          public std::enable_shared_from_this<DatadogCore> {
  struct CtorKey {
    explicit CtorKey() = default;
  };

 public:
  DatadogCore(const CtorKey&) {};
  DatadogCore(const DatadogCore&) = delete;
  DatadogCore& operator=(const DatadogCore&) = delete;

  template <typename T>
  void RegisterFeature();

  template <typename T>
  T* GetFeature() const {
    auto feature_opt = GetFeatureById(T::kFeatureId);
    return dynamic_cast<T*>(feature_opt);
    ;
  }

  static std::shared_ptr<DatadogCore> Create() {
    return std::make_shared<DatadogCore>(CtorKey());
  }

 private:
  explicit DatadogCore();

  void RegisterFeature(const FeatureId& feature_id,
                       std::unique_ptr<DatadogFeature> feature);
  DatadogFeature* GetFeatureById(const FeatureId& feature_id) const;

  std::unordered_map<FeatureId, std::unique_ptr<DatadogFeature>> features_;
};

template <typename T>
void DatadogCore::RegisterFeature() {
  using TFeatureId = decltype(T::kFeatureId);
  static_assert(
      std::is_trivially_assignable<FeatureId&, TFeatureId>::value &&
          std::is_const<TFeatureId>::value,
      "Datadog Feature is missing required const value for kFeatureId");
  auto feature = std::make_unique<T>(shared_from_this());
  RegisterFeature(T::kFeatureId, std::move(feature));
}

}  // namespace datadog::core
