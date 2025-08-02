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

class Logging : public FeatureBase
{
public:
    FeatureId GetId() const override { return CreateFeatureId("LOGS"); }
    std::string_view GetName() const override { return "logs"; }

protected:
    void Start() override;
    void Stop() override;

public:
    std::unique_ptr<Logger> CreateLogger(LoggerConfig& config);
};

}
