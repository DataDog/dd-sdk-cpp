#pragma once

#include "datadog/logging.h"
#include "datadog/logging.hpp"

namespace datadog {

inline LogLevel LogLevel_FromC(dd_log_level_t value)
{
    static_assert(static_cast<int>(LogLevel::Debug) == DD_LOG_LEVEL_DEBUG);
    static_assert(static_cast<int>(LogLevel::Info) == DD_LOG_LEVEL_INFO);
    static_assert(static_cast<int>(LogLevel::Notice) == DD_LOG_LEVEL_NOTICE);
    static_assert(static_cast<int>(LogLevel::Warn) == DD_LOG_LEVEL_WARN);
    static_assert(static_cast<int>(LogLevel::Error) == DD_LOG_LEVEL_ERROR);
    static_assert(static_cast<int>(LogLevel::Critical) == DD_LOG_LEVEL_CRITICAL);
    return static_cast<LogLevel>(value);
}

inline const char* LogLevel_ToString(LogLevel value)
{
    switch (value)
    {
        case LogLevel::Debug:
            return "Debug";
        case LogLevel::Info:
            return "Info";
        case LogLevel::Notice:
            return "Notice";
        case LogLevel::Warn:
            return "Warn";
        case LogLevel::Error:
            return "Error";
        case LogLevel::Critical:
            return "Critical";
        default:
            return "";
    }
}

inline LoggerConfig LoggerConfig_FromC(const dd_logger_config_t& config)
{
    return LoggerConfig{
        .remote_sample_rate = config.remote_sample_rate,
        .service = config.service,
        .name = config.name,
        .remote_log_threshold = LogLevel_FromC(config.remote_log_threshold),
    };
}

}
