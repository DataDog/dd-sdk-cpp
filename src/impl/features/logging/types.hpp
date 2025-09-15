// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

#include "datadog/logging.h"
#include "datadog/logging.hpp"

namespace datadog {

inline LogLevel LogLevel_FromC(dd_log_level_t value) {
  static_assert(static_cast<int>(LogLevel::Debug) == DD_LOG_LEVEL_DEBUG);
  static_assert(static_cast<int>(LogLevel::Info) == DD_LOG_LEVEL_INFO);
  static_assert(static_cast<int>(LogLevel::Notice) == DD_LOG_LEVEL_NOTICE);
  static_assert(static_cast<int>(LogLevel::Warn) == DD_LOG_LEVEL_WARN);
  static_assert(static_cast<int>(LogLevel::Error) == DD_LOG_LEVEL_ERROR);
  static_assert(static_cast<int>(LogLevel::Critical) == DD_LOG_LEVEL_CRITICAL);
  return static_cast<LogLevel>(value);
}

inline const char* LogLevel_ToString(LogLevel value) {
  switch (value) {
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Notice:
      return "notice";
    case LogLevel::Warn:
      return "warn";
    case LogLevel::Error:
      return "error";
    case LogLevel::Critical:
      return "critical";
    default:
      return "";
  }
}

}  // namespace datadog
