// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

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

inline LoggerConfig LoggerConfig_FromC(const dd_logger_config_t& config) {
  return LoggerConfig()
      .SetRemoteSampleRate(config.remote_sample_rate)
      .SetService(config.service)
      .SetName(config.name)
      .SetRemoteLogThreshold(LogLevel_FromC(config.remote_log_threshold))
      .SetInitialAttributeCapacity(config.initial_attribute_capacity)
      .SetEnrichWithRumContext(config.enrich_with_rum_context);
}

inline LogError LogError_FromC(const dd_log_error_t& err) {
  return LogError{
      err.message ? std::string_view{err.message} : std::string_view{},
      err.kind ? std::string_view{err.kind} : std::string_view{},
      err.stack ? std::string_view{err.stack} : std::string_view{}
  };
}

}  // namespace datadog
