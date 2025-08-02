#include "logging/logging.hpp"

#include "core/core.hpp"

namespace datadog::impl {

void Logger::Log(LogLevel level, std::string_view message)
{
}

bool Logging::Register(Core& core)
{
    return core.RegisterFeature(
        CreateFeatureId("LOGS"), 
        "logs",
        [this](StorageWriter writer) { OnStart(writer); },
        [this]() { OnStop(); }
    );
}

std::unique_ptr<Logger> Logging::CreateLogger(LoggerConfig& config)
{
    return nullptr;
}

void Logging::OnStart(StorageWriter writer)
{
    _writer = writer;
    _writer("hello world", "");
}

void Logging::OnStop()
{
    _writer("goodbye", "metadata");
    _writer = nullptr;
}

}
