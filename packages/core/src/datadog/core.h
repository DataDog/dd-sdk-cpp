// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <chrono>
#include <memory>
#include <type_traits>
#include <unordered_map>

#include <functional>
#include "datadog/feature.h"
#include "datadog/internal/writer.h"
#include "datadog/time_provider.h"
#include "datadog/version.h"

namespace datadog::core {

using datadog::core::FeatureId;

// Configuration for DatadogCore
struct DatadogCoreConfiguration {
  std::string application_name;
  std::string application_version;
  std::string client_token;
  DateTimeProvider time_provider{DefaultTimeProvider};
};

// Current state tracked by the DatadogCore. Includes the global
// configuration state as well as ephemeral state advertised by various
// Features, such as the current RUM view or session id.
class DatadogCoreContext {
 public:
  DatadogCoreContext(const DatadogCoreConfiguration& config);

  // TODO: More examples of context (OS information, device information)
  constexpr std::string_view GetSdkVersion() const noexcept {
    return kSdkVersion;
  }
  constexpr const std::string& GetApplicationName() const noexcept {
    return application_name_;
  }
  constexpr const std::string& GetApplicationVersion() const noexcept {
    return application_version_;
  }

 private:
  std::string application_name_;
  std::string application_version_;
};

class IDatadogCore {
 public:
  virtual ~IDatadogCore() = default;

  virtual const DateTimeProvider GetTimeProvider() const noexcept = 0;

  virtual void Write(FeatureId feature,
                     std::function<void(const DatadogCoreContext&,
                                        datadog::core::internal::Writer*)>
                         write_callback) const = 0;
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
  DatadogCore(const CtorKey&, const DatadogCoreConfiguration& config);
  DatadogCore(const DatadogCore&) = delete;
  DatadogCore& operator=(const DatadogCore&) = delete;

  template <typename T>
  void RegisterFeature();

  template <typename T>
  T* GetFeature() const {
    auto feature_opt = GetFeatureById(T::feature_id);
    return dynamic_cast<T*>(feature_opt);
  }

  virtual const DateTimeProvider GetTimeProvider() const noexcept override {
    return time_provider_;
  }

  virtual void Write(FeatureId feature,
                     std::function<void(const DatadogCoreContext&,
                                        datadog::core::internal::Writer*)>
                         write_callback) const override;

  static std::shared_ptr<DatadogCore> Create(
      const DatadogCoreConfiguration& config) {
    return std::make_shared<DatadogCore>(CtorKey(), config);
  }

 private:
  void RegisterFeature(FeatureId feature_id,
                       std::unique_ptr<DatadogFeature> feature);
  DatadogFeature* GetFeatureById(FeatureId feature_id) const;

  DateTimeProvider time_provider_;
  DatadogCoreContext context_;
  std::unordered_map<FeatureId, std::unique_ptr<DatadogFeature>> features_;
};

template <typename T>
void DatadogCore::RegisterFeature() {
  using TFeatureId = decltype(T::feature_id);
  static_assert(
      std::is_same_v<const FeatureId, TFeatureId>,
      "Datadog Feature is missing required const value for feature_id");
  auto feature = std::make_unique<T>(shared_from_this());
  RegisterFeature(T::feature_id, std::move(feature));
}

}  // namespace datadog::core
