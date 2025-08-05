#pragma once

#include "core/feature.hpp"
#include "features/logging/types.hpp"

namespace datadog::impl {

struct Logger
{
    void Log(LogLevel level, std::string_view message);

private:
    LoggerConfig _config;
};

class Logging : public Feature
{
public:
    FeatureId GetId() const override { return CreateFeatureId("LOGS"); }
    std::string_view GetName() const override { return "logs"; }

    std::optional<Report> PrepareReport(BatchReader& reader) override;

protected:
    void Start() override;
    void Stop() override;

public:
    std::unique_ptr<Logger> CreateLogger(LoggerConfig& config);
};

}
