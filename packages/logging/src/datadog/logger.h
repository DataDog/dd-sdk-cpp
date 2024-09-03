// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <datadog/core.h>

namespace datadog::logging {

using datadog::core::DateTimeProvider;
using datadog::core::IDatadogCore;

enum class LogStatus {
  debug,
  info,
  notice,
  warning,
  error,
  critical,
  emergency,
};

struct LoggerConfiguration {
  std::string service_name;
  std::string environment;
  std::string logger_name;
  std::string logger_version;
};

class Logger {
 public:
  explicit Logger(const LoggerConfiguration& options,
                  const std::weak_ptr<IDatadogCore>& core);

  constexpr void Debug(std::string_view message) {
    Log(LogStatus::debug, message);
  }

  void Log(LogStatus log_status, std::string_view message);

 private:
  const LoggerConfiguration configuration_;
  std::weak_ptr<IDatadogCore> core_;
};

}  // namespace datadog::logging
