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
  return static_cast<uint32_t>(a) << 0 | static_cast<uint32_t>(b) << 8 |
         static_cast<uint32_t>(c) << 16 | static_cast<uint32_t>(d) << 24;
}

class DatadogCore {
 public:
  template <typename T>
  void RegisterFeature() {
    using TFeatureId = decltype(T::kFeatureId);
    static_assert(
        std::is_integral<TFeatureId>::value && std::is_const<TFeatureId>::value,
        "Datadog Feature is missing required const value for kFeatureId");
    auto feature = std::make_unique<T>();
    RegisterFeature(T::kFeatureId, std::move(feature));
  }

  template <typename T>
  T* GetFeature() const {
    auto feature_opt = GetFeatureById(T::kFeatureId);
    return dynamic_cast<T*>(feature_opt);
    ;
  }

 private:
  void RegisterFeature(const FeatureId& feature_id,
                       std::unique_ptr<DatadogFeature> feature);
  DatadogFeature* GetFeatureById(const FeatureId& feature_id) const;

  std::unordered_map<FeatureId, std::unique_ptr<DatadogFeature>> features_;
};

}  // namespace datadog::core
