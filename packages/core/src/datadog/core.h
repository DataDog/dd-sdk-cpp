// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <type_traits>
#include <unordered_map>

#include <functional>
#include "datadog/core_message.h"
#include "datadog/feature.h"
#include "datadog/internal/writer.h"
#include "datadog/time_provider.h"
#include "datadog/version.h"

namespace datadog::core {

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
  explicit DatadogCoreContext(const DatadogCoreConfiguration& config);

  // TODO: More examples of context (OS information, device information)
  std::string_view GetSdkVersion() const noexcept { return kSdkVersion; }
  const std::string& GetApplicationName() const noexcept {
    return application_name_;
  }
  const std::string& GetApplicationVersion() const noexcept {
    return application_version_;
  }

 private:
  std::string application_name_;
  std::string application_version_;
};

class IDatadogCore {
 protected:
  enum class Allow { ctor };

 public:
  virtual ~IDatadogCore() = default;

  virtual uint64_t GetNow() const noexcept = 0;
  virtual const DatadogCoreContext& GetCoreContext() const noexcept = 0;

  virtual void SendMesage(FeatureId feature, const CoreMessage& msg) = 0;
};

// DatadogCore is the integration point for individual features that wish to
// send data to Datadog intake. "Features" include Logs, Traces, and Real User
// Monitoring. DatadogCore is responsible for gathering data for individual
// features, caching or storing the data, and sending that data to the proper
// intake endpoint.
class DatadogCore final : public IDatadogCore,
                          public std::enable_shared_from_this<DatadogCore> {
 public:
  explicit DatadogCore(IDatadogCore::Allow allow,
                       const DatadogCoreConfiguration& config);
  DatadogCore(const DatadogCore&) = delete;
  DatadogCore& operator=(const DatadogCore&) = delete;

  template <typename T>
  void RegisterFeature();

  template <typename T>
  T* GetFeature() const {
    auto feature_opt = FindFeatureById(T::feature_id);
    return dynamic_cast<T*>(feature_opt);
  }

  uint64_t GetNow() const noexcept override { return time_provider_(); }

  virtual const DatadogCoreContext& GetCoreContext() const noexcept override {
    return context_;
  }

  virtual void SendMesage(FeatureId feature, const CoreMessage& msg) override;

  static std::shared_ptr<DatadogCore> Create(
      const DatadogCoreConfiguration& config) {
    return std::make_shared<DatadogCore>(Allow::ctor, config);
  }

 private:
  void RegisterFeature(FeatureId feature_id,
                       std::unique_ptr<DatadogFeature> feature);
  DatadogFeature* FindFeatureById(FeatureId feature_id) const;
  void ProcessWrites();

  DateTimeProvider time_provider_;
  DatadogCoreContext context_;

  std::list<std::pair<FeatureId, std::string>> write_queue_;
  std::unordered_map<FeatureId, std::unique_ptr<DatadogFeature>>
      features_by_id_;
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
