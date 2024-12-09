// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <optional>

#include "datadog/attribute.h"
#include "datadog/logging_feature.h"

namespace datadog::logging {

using datadog::core::DatadogAttribute;

class DatadogLogger {
 public:
  explicit DatadogLogger(const DatadogLogConfiguration& configuration,
                         const std::weak_ptr<DatadogLogging>& logging_feature)
      : configuration_{configuration},
        feature_{logging_feature},
        logger_attributes_{DatadogAttribute::Type::Object} {}

  void Log(LogLevel level,
           std::string_view message,
           const DatadogAttribute& attribues);

  void AddAttribute(std::string_view name, const DatadogAttribute& value);
  void RemoveAttribute(std::string_view name);

  void Debug(std::string_view message,
             const DatadogAttribute& attributes = DatadogAttribute::kNull) {
    Log(LogLevel::Debug, message, attributes);
  }
  void Info(std::string_view message,
            const DatadogAttribute& attributes = DatadogAttribute::kNull) {
    Log(LogLevel::Info, message, attributes);
  }
  void Notice(std::string_view message,
              const DatadogAttribute& attributes = DatadogAttribute::kNull) {
    Log(LogLevel::Notice, message, attributes);
  }
  void Warn(std::string_view message,
            const DatadogAttribute& attributes = DatadogAttribute::kNull) {
    Log(LogLevel::Warn, message, attributes);
  }
  void Error(std::string_view message,
             const DatadogAttribute& attributes = DatadogAttribute::kNull) {
    Log(LogLevel::Error, message, attributes);
  }
  void Critical(std::string_view message,
                const DatadogAttribute& attributes = DatadogAttribute::kNull) {
    Log(LogLevel::Critical, message, attributes);
  }

 private:
  DatadogLogConfiguration configuration_;
  std::weak_ptr<DatadogLogging> feature_;
  core::DatadogAttribute logger_attributes_;
};

}  // namespace datadog::logging
