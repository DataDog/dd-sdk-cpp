#include "features/logging/logging.hpp"

#include <array>
#include <mutex>
#include <shared_mutex>
#include <string_view>

#include "date/date.h"

#include "attribute/json.hpp"
#include "attribute/merge.hpp"
#include "core/block.hpp"
#include "core/core.hpp"
#include "core/version.hpp"
#include "core/writer.hpp"
#include "features/logging/logger.hpp"

namespace datadog::impl {

static constexpr bool is_valid_user_attribute_name(std::string_view name)
{
    // clang-format off
    if (name == "status") { return false; }
    if (name == "service") { return false; }
    if (name == "message") { return false; }
    if (name == "date") { return false; }
    if (name == "logger") { return false; }
    if (name == "_dd") { return false; }
    if (name == "usr") { return false; }
    if (name == "account") { return false; }
    if (name == "network") { return false; }
    if (name == "error") { return false; }
    if (name == "build_id") { return false; }
    if (name == "ddtags") { return false; }
    // clang-format on
    return true;
}

Logging::Logging(
    const platform::IClock& clock,
    std::string_view service_name,
    std::string_view application_version
)
    : _clock(clock)
    , _sdk_version(SDK_VERSION)
    , _default_service_name(service_name)
    , _application_version(application_version)
    , _global_attributes(8)
{
    _request_url.reserve(256);
    _request_headers.reserve(512);
}

std::optional<Report>
Logging::UploadThread_PrepareReport(const CoreContext& context, BatchReader& reader)
{
    // Request URL
    static const std::string_view request_path = "/api/v2/logs";
    static const bool with_ddsource = true;

    // Request headers
    static const std::string_view content_type = "application/json";

    // Rebuild URL and headers only if context has changed, reusing existing strings
    if (_last_context_version != context.version)
    {
        context.BuildRequestURL(request_path, with_ddsource, _request_url);
        context.BuildRequestHeaders(content_type, "", _request_headers);
        _last_context_version = context.version;
    }

    // Each event in the batch is a JSON object: initialize a writer that will
    // concatenate each of those objects into a JSON array
    return Report{_request_url, _request_headers, TLVBatchWriter{reader}};
}

void Logging::SetAttribute(std::string_view name, const Attribute& value)
{
    std::unique_lock exclusive_write_lock(_global_attributes_mutex);
    _global_attributes.attribute.SetObjectProperty(name, value);
}

void Logging::DeleteAttribute(std::string_view name)
{
    std::unique_lock exclusive_write_lock(_global_attributes_mutex);
    _global_attributes.attribute.DeleteObjectProperty(name);
}

std::unique_ptr<Logger> Logging::CreateLogger(const LoggerConfig& config)
{
    std::weak_ptr<Logging> self = std::static_pointer_cast<Logging>(shared_from_this());
    auto event_callback = [self](
                              Attribute& mut_event_object,
                              std::vector<uint8_t>& mut_event_buffer,
                              const StringAttribute& logger_service_name,
                              const ObjectAttribute& logger_object,
                              const Attribute& logger_attributes,
                              LogLevel level,
                              std::string_view message,
                              const Attribute& message_attributes
                          )
    {
        if (auto logging = self.lock())
        {
            logging->OnLoggerEmit(
                mut_event_object,
                mut_event_buffer,
                logger_service_name,
                logger_object,
                logger_attributes,
                level,
                message,
                message_attributes
            );
        }
    };
    return std::make_unique<Logger>(config, event_callback);
}

void Logging::OnLoggerEmit(
    Attribute& mut_event_object,
    std::vector<uint8_t>& mut_event_buffer,
    const StringAttribute& logger_service_name,
    const ObjectAttribute& logger_object,
    const Attribute& logger_attributes,
    LogLevel level,
    std::string_view message,
    const Attribute& message_attributes
) const
{
    // Set the 'status' field based on the log level, using static Attribute strings
    mut_event_object.SetObjectProperty("status", Logging::GetLogLevelString(level));

    // Set 'service': use the logger-overridden value if set, otherwise use the global
    // default from SDK config
    if (logger_service_name && !logger_service_name.attribute.GetStringValue().empty())
    {
        mut_event_object.SetObjectProperty("service", *logger_service_name);
    }
    else
    {
        mut_event_object.SetObjectProperty("service", *_default_service_name);
    }

    // Set 'date' from the current timestamp
    const platform::Timestamp now = _clock.Now();
    mut_event_object.SetObjectProperty("date", Attribute::Timestamp(now));

    // Set 'message'
    // TODO: Reuse string memory on object property value set
    mut_event_object.SetObjectProperty("message", Attribute::String(message));

    // Set the 'logger' object with the details of the logger
    mut_event_object.SetObjectProperty("logger", *logger_object);

    // Merge in all user-supplied attributes, ignoring any properties with reserved
    // names (ensuring that all of the standard properties set above are preserved), and
    // preferring the latter value in case of name conflicts
    std::shared_lock read_only_lock(_global_attributes_mutex);
    AttributeMerge::AssembleObject(
        mut_event_object,
        {*_global_attributes, logger_attributes, message_attributes},
        is_valid_user_attribute_name
    );
    read_only_lock.unlock();

    // Serialize to JSON, using the logger-owned buffer to ensure that it's safe to
    // serialize multiple messages from different loggers concurrently
    AttributeSerialization::ToJSON(mut_event_object, mut_event_buffer);

    WriteEvent(
        Block{
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<const char*>(mut_event_buffer.data()),
            mut_event_buffer.size()
        }
    );
}

Attribute Logging::GetLogLevelString(LogLevel level)
{
    // Establish a set of read-only string attributes for each of our log level names,
    // with static storage
    static Attribute debug = Attribute::String(LogLevel_ToString(LogLevel::Debug));
    static Attribute info = Attribute::String(LogLevel_ToString(LogLevel::Info));
    static Attribute notice = Attribute::String(LogLevel_ToString(LogLevel::Notice));
    static Attribute warn = Attribute::String(LogLevel_ToString(LogLevel::Warn));
    static Attribute error = Attribute::String(LogLevel_ToString(LogLevel::Error));
    static Attribute critical =
        Attribute::String(LogLevel_ToString(LogLevel::Critical));

    // Return the appropriate attribute value for the given log level
    switch (level)
    {
        case LogLevel::Debug:
            return debug;
        case LogLevel::Info:
            return info;
        case LogLevel::Notice:
            return notice;
        case LogLevel::Warn:
            return warn;
        case LogLevel::Error:
            return error;
        case LogLevel::Critical:
            return critical;
    }
    return Attribute::Null();
}

} // namespace datadog::impl
