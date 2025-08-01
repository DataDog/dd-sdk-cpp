#include "logging/logging.hpp"

#include "core/core.hpp"

namespace datadog::impl {

void Logger::Log(LogLevel level, std::string_view message)
{
}

void Logging::Register(Core& core)
{
    core.RegisterFeature(CreateFeatureId("LOGS"), "logs");
}

std::unique_ptr<Logger> Logging::CreateLogger(LoggerConfig& config)
{
    return nullptr;
}

}
