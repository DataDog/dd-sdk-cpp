#include "features/logging/logging.hpp"

#include "core/core.hpp"
#include "platform/http_writer.hpp"

namespace datadog::impl {

void Logger::Log(LogLevel level, std::string_view message) {}

Logging::Logging()
    : _last_context_version(0)
{
    _request_url.reserve(256);
    _request_headers.reserve(512);
}

void Logging::Start()
{
    WriteEvent("hello world");
}

void Logging::Stop()
{
    WriteEvent("goodbye", "metadata");
}

std::optional<Report>
Logging::UploadThread_PrepareReport(const CoreContext& context, BatchReader& reader)
{
    // Request URL
    static const std::string_view request_path = "/api/v2/logs";
    static const bool with_ddsource = true;

    // Request headers
    static const std::string_view content_type = "application/json";
    static const std::string_view feature_headers = "X-Cool: beans\n";

    // Rebuild URL and headers only if context has changed, reusing existing strings
    if (_last_context_version != context.version)
    {
        context.BuildRequestURL(request_path, with_ddsource, _request_url);
        context.BuildRequestHeaders(content_type, feature_headers, _request_headers);
        _last_context_version = context.version;
    }

    // TODO: Read TLV blocks
    std::string body = "{}";

    return Report{ _request_url, _request_headers, platform::StringWriter(body) };
}

std::unique_ptr<Logger> Logging::CreateLogger(LoggerConfig& config)
{
    return nullptr;
}

}
