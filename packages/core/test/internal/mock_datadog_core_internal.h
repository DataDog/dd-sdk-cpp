// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include "datadog/internal/core_internal.h"

#include <catch2/trompeloeil.hpp>

#include "storage/mock_datadog_file_system.h"

namespace datadog::core::mocks {

using datadog::core::internal::DatadogCoreInternal;
using datadog::core::internal::FeatureInfo;
using datadog::core::internal::PerformancePreset;
using datadog::core::storage::DatadogFileSystem;
using datadog::core::storage::FeatureStorage;
using datadog::core::storage::mocks::MockDatadogFileSystem;

class MockDatadogCoreInternal : public DatadogCoreInternal {
 public:
  MockDatadogCoreInternal(Allow allow)
      : DatadogCoreInternal(allow),
        file_system{std::make_shared<MockDatadogFileSystem>()} {}

  MAKE_MOCK0(GetNow, Nanoseconds(), const override);
  MAKE_MOCK0(Start, void(), override);
  MAKE_MOCK0(IsRunning, bool(), const override);
  MAKE_MOCK0(Shutdown, void(), override);
  MAKE_MOCK0(GetContext, const internal::CoreContext&(), const override);
  MAKE_MOCK1(SendMessage, void(CoreMessage&&), override);
  MAKE_MOCK0(CreateReporter, void(), override);
  MAKE_MOCK0(GetReporter,
             std::shared_ptr<reporting::DatadogReporter>(),
             const override);

  void RegisterFeature(
      FeatureId feature_id,
      const std::shared_ptr<DatadogFeature>& feature) override {
    std::shared_ptr<DatadogFileSystem> feature_file_system{
        file_system->CreateChildFileSystem(feature->GetName())};
    FeatureInfo feature_info{
        feature_id,
        feature,
        std::make_unique<FeatureStorage>(std::string{feature->GetName()},
                                         performance_preset, time_provider,
                                         feature_file_system),
    };
    feature_info_by_id.emplace(feature_id, std::move(feature_info));
  }

  bool FeatureExists(FeatureId feature_id) const override {
    return feature_info_by_id.find(feature_id) != feature_info_by_id.end();
  };
  std::shared_ptr<DatadogFeature> GetFeatureById(
      FeatureId feature_id) const override {
    if (const auto& it = feature_info_by_id.find(feature_id);
        it != feature_info_by_id.end()) {
      return it->second.feature;
    }

    return nullptr;
  }
  const FeatureInfoMap& GetFeatureInfoMap() const override {
    return feature_info_by_id;
  }

  PerformancePreset performance_preset{BatchSize::Medium,
                                       UploadFrequency::Average,
                                       BatchProcessingLevel::Medium};
  DateTimeProvider time_provider = DefaultDateTimeProvider;
  FeatureInfoMap feature_info_by_id;
  std::shared_ptr<MockDatadogFileSystem> file_system;

  static std::shared_ptr<MockDatadogCoreInternal> Create() {
    return std::make_shared<MockDatadogCoreInternal>(Allow{});
  }
};

}  // namespace datadog::core::mocks
