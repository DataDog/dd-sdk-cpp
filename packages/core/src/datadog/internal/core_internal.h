// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.

#include "datadog/core.h"

#include "datadog/core_message.h"
#include "datadog/internal/core_context.h"

namespace datadog::core::internal {

// Internal interface to DatadogCore, for use internally by Datadog modules
// only. Abstract to allow for mocking in testing.
class DatadogCoreInternal : public DatadogCore {
 public:
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

  static std::shared_ptr<DatadogCoreInternal> Create(
      const DatadogConfiguration& configuration);
};

}  // namespace datadog::core::internal
