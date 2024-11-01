// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <map>
#include <memory>
#include <thread>
#include <type_traits>

#include "datadog/core_configuration.h"
#include "datadog/core_message.h"
#include "datadog/feature.h"
#include "datadog/internal/core_context.h"
#include "datadog/time_provider.h"

namespace datadog::core {

using Nanoseconds = std::chrono::duration<uint64_t, std::nano>;

class DatadogCore : public std::enable_shared_from_this<DatadogCore> {
 protected:
  enum class Allow { ctor };

 public:
  explicit DatadogCore(Allow) {}
  virtual ~DatadogCore() = default;

  DatadogCore(const DatadogCore&) = delete;
  DatadogCore& operator=(const DatadogCore&) = delete;

  virtual Nanoseconds GetNow() const = 0;
  // REVISIT: It might be better to pass the initial context during feature
  // initialization, then broadcast changes via the ContextChanged API that
  // we're planning on features
  virtual const internal::CoreContext& GetContext() const = 0;
  virtual void SendMessage(CoreMessage&& msg) = 0;
  virtual bool FeatureExists(FeatureId feature_id) const = 0;

  template <typename T, typename... Args>
  std::shared_ptr<T> RegisterFeature(Args&&... args);

  virtual void Shutdown() = 0;

  static std::shared_ptr<DatadogCore> Create(
      const DatadogConfiguration& configuration);

 private:
  virtual void RegisterFeature(
      FeatureId,
      const std::shared_ptr<DatadogFeature>& feature) = 0;
};

template <typename T, typename... Args>
std::shared_ptr<T> DatadogCore::RegisterFeature(Args&&... args) {
  using TFeatureId = decltype(T::feature_id);
  static_assert(
      std::is_same_v<const FeatureId, TFeatureId>,
      "Datadog Feature is missing required const value for feature_id");

  if (FeatureExists(T::feature_id)) {
    // TODO(jeff.ward): Add telemetry / logging that a feature is being
    // registered twice
    return nullptr;
  }

  auto feature = std::make_shared<T>(weak_from_this(), std::forward(args)...);
  RegisterFeature(T::feature_id, feature);
  return feature;
}

}  // namespace datadog::core
