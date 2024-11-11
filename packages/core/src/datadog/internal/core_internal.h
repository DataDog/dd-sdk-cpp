// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include "datadog/core.h"

#include "datadog/core_message.h"
#include "datadog/internal/core_context.h"
#include "datadog/internal/core_feature_info.h"

namespace datadog::core::internal {

// Internal interface to DatadogCore, for use internally by Datadog modules
// only. Abstract to allow for mocking in testing.
class DatadogCoreInternal : public DatadogCore {
 public:
  using FeatureInfoMap = std::map<FeatureId, FeatureInfo>;

  explicit DatadogCoreInternal(DatadogCore::Allow allow) : DatadogCore(allow) {}
  ~DatadogCoreInternal() override = default;

  DatadogCoreInternal(const DatadogCoreInternal&) = delete;
  DatadogCoreInternal& operator=(const DatadogCoreInternal&) = delete;

  template <typename T>
  std::shared_ptr<T> GetFeature() const {
    return std::dynamic_pointer_cast<T>(GetFeatureById(T::feature_id));
  }
  virtual std::shared_ptr<DatadogFeature> GetFeatureById(
      FeatureId feature_id) const = 0;

  // Internal interfaces for use within Datadog only
  virtual const FeatureInfoMap& GetFeatureInfoMap() const = 0;
  // Create the reporter from the configured creation function if it does not
  // exist. Otherwise this function does nothing. This function exists to assist
  // in unit testing the core where creating the reporter as part of Start is
  // not desirable.
  virtual void CreateReporter() = 0;
  virtual std::shared_ptr<reporting::DatadogReporter> GetReporter() const = 0;

  static std::shared_ptr<DatadogCoreInternal> Create(
      const DatadogConfiguration& configuration);
};

}  // namespace datadog::core::internal
