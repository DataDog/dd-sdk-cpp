// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <optional>

#include "datadog/logging_feature.h"

namespace datadog::logging {

class DatadogLogger {
 public:
  explicit DatadogLogger(const DatadogLogConfiguration& configuration,
                         const std::weak_ptr<DatadogLogging>& logging_feature)
      : configuration_{configuration}, feature_{logging_feature} {}

  void Log(LogLevel level, std::string_view message);

  void Debug(std::string_view message) { Log(LogLevel::Debug, message); }
  void Info(std::string_view message) { Log(LogLevel::Info, message); }
  void Notice(std::string_view message) { Log(LogLevel::Notice, message); }
  void Warn(std::string_view message) { Log(LogLevel::Warn, message); }
  void Error(std::string_view message) { Log(LogLevel::Error, message); }
  void Critical(std::string_view message) { Log(LogLevel::Critical, message); }

 private:
  DatadogLogConfiguration configuration_;
  std::weak_ptr<DatadogLogging> feature_;
};

}  // namespace datadog::logging
