#pragma once

#include "core/feature.hpp"
#include "logging/types.hpp"

namespace datadog::impl {

struct Logger
{
    void Log(LogLevel level, std::string_view message);

private:
    LoggerConfig _config;
};

struct Logging
{
    bool Register(Core& core);

    std::unique_ptr<Logger> CreateLogger(LoggerConfig& config);
};

}
