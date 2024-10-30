// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <memory>
#include <string_view>

#include <datadog/core.h>

namespace datadog::logging {

class DatadogLogger;

enum class LogLevel {
  Debug,
  Info,
  Notice,
  Warn,
  Error,
  Critical,
};

struct DatadogLogConfiguration {
  float remote_sample_rate = 1.0;
  std::optional<std::string> service;
  std::optional<std::string> name;
  LogLevel remote_log_threshold = LogLevel::Debug;
};

class DatadogLogging : public core::DatadogFeature,
                       public std::enable_shared_from_this<DatadogLogging> {
 public:
  explicit DatadogLogging(const std::weak_ptr<core::IDatadogCore>& core)
      : core_(core) {}

  std::string_view GetName() const override { return "logs"; }

  static constexpr core::FeatureId feature_id =
      core::internal::CreateFourCC('L', 'O', 'G', 'S');

  std::unique_ptr<DatadogLogger> CreateLogger(
      const DatadogLogConfiguration& configuration);

  std::shared_ptr<core::IDatadogCore> GetCore() { return core_.lock(); }

 private:
  std::weak_ptr<core::IDatadogCore> core_;
};

}  // namespace datadog::logging
