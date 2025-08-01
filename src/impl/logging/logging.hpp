#pragma once

#include "core/feature_id.hpp"
#include "core/core.hpp"
#include "logging/types.hpp"

namespace datadog::impl {

using LogLevel = datadog::LogLevel;
using LoggerConfig = datadog::LoggerConfig;

struct Logger
{
    void Log(LogLevel level, std::string_view message);

private:
    LoggerConfig _config;
};

struct Logging
{
    void Register(Core& core);

    std::unique_ptr<Logger> CreateLogger(LoggerConfig& config);
};

}
