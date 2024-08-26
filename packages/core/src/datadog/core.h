// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <map>
#include <optional>

#include "datadog/feature.h"

namespace datadog::core {

constexpr FeatureId four_cc(unsigned char a,
                            unsigned char b,
                            unsigned char c,
                            unsigned char d) {
  return a << 24 | b << 16 | c << 8 | d;
}

class DatadogCore {
 public:
  template <typename T>
  void RegisterFeature() {
    auto feature = std::make_unique<T>();
    RegisterFeature(T::kFeatureId, std::move(feature));
  }

  template <typename T>
  std::optional<T*> GetFeature() const {
    auto feature_opt = GetFeatureById(T::kFeatureId);
    if (feature_opt.has_value()) {
      return std::optional(dynamic_cast<T*>(feature_opt.value()));
    }

    return std::nullopt;
  }

 private:
  void RegisterFeature(const FeatureId& feature_id,
                       std::unique_ptr<DatadogFeature> feature);
  std::optional<DatadogFeature*> GetFeatureById(
      const FeatureId& feature_id) const;

  std::unordered_map<FeatureId, std::unique_ptr<DatadogFeature>> features_;
};

}  // namespace datadog::core
