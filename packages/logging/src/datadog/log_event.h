// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "datadog/logger.h"

namespace datadog::logging {

struct LogError {
  std::optional<std::string> kind;
  std::optional<std::string> message;
  std::optional<std::string> stack;
  std::string source_type;
  std::optional<std::string> fingerprint;
};

// LogEvents are what get written to Datadog intake
struct LogEvent {
  uint64_t date;
  LogStatus status;
  std::string message;
  std::optional<LogError> error;
  std::string application_version;
  std::string_view service_name;
  std::string_view environment;
  std::string_view logger_name;
  std::string_view logger_version;
};

void EncodeLogEvent(const LogEvent& log_event, std::stringstream& target) {
  // TODO: std::stringstream will likely be replaced by JSON serialization
}

}  // namespace datadog::logging
