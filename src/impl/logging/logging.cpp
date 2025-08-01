#include "logging/logging.hpp"

#include "core/core.hpp"

namespace datadog::impl {

void Logger::Log(LogLevel level, std::string_view message)
{
}

bool Logging::Register(Core& core)
{
    auto writer = core.RegisterFeature(CreateFeatureId("LOGS"), "logs");
    if (!writer)
    {
        return false;
    }

    writer("hello world", "");

    return true;
}

std::unique_ptr<Logger> Logging::CreateLogger(LoggerConfig& config)
{
    return nullptr;
}

}
