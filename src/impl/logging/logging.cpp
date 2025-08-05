#include "logging/logging.hpp"

#include "core/core.hpp"

namespace datadog::impl {

void Logger::Log(LogLevel level, std::string_view message)
{
}

void Logging::Start()
{
    WriteEvent("hello world");
}

void Logging::Stop()
{
    WriteEvent("goodbye", "metadata");
}

std::optional<Report> Logging::PrepareReport(BatchReader& reader)
{
    return std::nullopt;
}

std::unique_ptr<Logger> Logging::CreateLogger(LoggerConfig& config)
{
    return nullptr;
}

}
