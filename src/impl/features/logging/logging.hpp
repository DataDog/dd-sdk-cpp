#pragma once

#include <cinttypes>
#include <string>

#include "core/feature.hpp"
#include "features/logging/types.hpp"

namespace datadog::impl {

class Logger
{
public:
    void Log(LogLevel level, std::string_view message);

private:
    LoggerConfig _config;
};

class Logging final : public Feature
{
public:
    Logging();

    FeatureId GetId() const override
    {
        return CreateFeatureId("LOGS");
    }
    std::string_view GetName() const override
    {
        return "logs";
    }

    std::optional<Report> UploadThread_PrepareReport(
        const CoreContext& context,
        BatchReader& reader
    ) override;

protected:
    void Start() override;
    void Stop() override;

public:
    std::unique_ptr<Logger> CreateLogger(LoggerConfig& config);

private:
    int32_t _last_context_version{ 0 };
    std::string _request_url;
    std::string _request_headers;
};

} // namespace datadog::impl
