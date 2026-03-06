// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/logging/logging.hpp"

#include <array>
#include <mutex>
#include <shared_mutex>
#include <string_view>

#include "date/date.h"

#include "datadog/impl/attribute/merge.hpp"
#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature_types/logging.hpp"
#include "datadog/impl/core/version.hpp"
#include "datadog/impl/core/writer.hpp"
#include "datadog/impl/features/logging/logger.hpp"
#include "datadog/impl/json.hpp"

namespace datadog::impl {

/**
 * Parameters captured synchronously on the caller thread and passed to the context
 * thread for async log event processing.
 */
struct LogCommandParams {
  Timestamp timestamp;
  LogLevel level;
  std::string message;
  LoggerEnrichmentConfig enrichment;
  Attribute global_attributes;
  Attribute logger_attributes;
  Attribute message_attributes;
  std::string service;
  std::string logger_name;
};

Logging::Logging(
    const platform::IClock& clock,
    std::string_view service_name,
    std::string_view application_version
)
    : _clock(clock),
      _sdk_version(SDK_VERSION),
      _default_service_name(service_name),
      _application_version(application_version),
      _global_attributes(8) {
  _request_url.reserve(256);
  _request_headers.reserve(512);
}

std::optional<Report> Logging::UploadThread_PrepareReport(
    const HttpContext& context, BatchReader& reader
) {
  // Request URL
  static const std::string_view request_path = "/api/v2/logs";
  static const bool with_ddsource = true;

  // Request headers
  static const std::string_view content_type = "application/json";

  // Build URL and headers once, on the first upload (HTTP context is immutable)
  if (_request_url.empty()) {
    context.BuildRequestURL(request_path, with_ddsource, _request_url);
    context.BuildRequestHeaders(content_type, "", _request_headers);
  }

  // Each event in the batch is a JSON object: initialize a writer that will concatenate
  // each of those objects into a JSON array
  return Report{_request_url, _request_headers, TLVBatchWriter{reader}};
}

void Logging::AddAttribute(std::string_view name, const Attribute& value) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.SetObjectProperty(name, value);
}

void Logging::RemoveAttribute(std::string_view name) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.DeleteObjectProperty(name);
}

std::unique_ptr<Logger> Logging::CreateLogger(const LoggerConfig& config) {
  std::weak_ptr<Logging> self = std::static_pointer_cast<Logging>(shared_from_this());
  auto event_callback = [self](
                            LoggerState& mut_state,
                            const LoggerEnrichmentConfig& enrichment,
                            LogLevel level,
                            std::string_view message,
                            const Attribute& message_attributes
                        ) {
    if (auto logging = self.lock()) {
      logging->OnLoggerEmit(mut_state, enrichment, level, message, message_attributes);
    }
  };
  return std::make_unique<Logger>(config, event_callback);
}

void Logging::OnLoggerEmit(
    LoggerState& mut_state,
    const LoggerEnrichmentConfig& enrichment,
    LogLevel level,
    std::string_view message,
    const Attribute& message_attributes
) {
  // Phase A: Capture time-sensitive values synchronously on caller thread

  // Capture timestamp immediately
  Timestamp timestamp = _clock.Now();

  // Snapshot logger attributes
  Attribute logger_attributes = mut_state.user_attributes.attribute;

  // Snapshot global attributes with read lock
  Attribute global_attributes;
  {
    std::shared_lock read_lock(_global_attributes_mutex);
    global_attributes = _global_attributes.attribute;
  }

  // Resolve service name
  std::string service =
      mut_state.event.service.empty() ? _default_service_name : mut_state.event.service;

  // Copy logger name (OmitIfEmpty needs .value access)
  std::string logger_name = mut_state.event.logger_name.value.empty()
                                ? ""
                                : mut_state.event.logger_name.value;

  // Package into LogCommandParams
  LogCommandParams params{
      timestamp,
      level,
      std::string(message),
      enrichment,
      global_attributes,
      logger_attributes,
      message_attributes,
      service,
      logger_name
  };

  // Dispatch async processing
  DispatchAsync(params);
}

void Logging::DispatchAsync(const LogCommandParams& params) {
  // Phase B: Dispatch to context thread with weak_ptr for shutdown safety

  if (!_scope) {
    return;
  }

  // Store reference to scope to satisfy linter's optional access check
  FeatureScope& scope = *_scope;

  // Capture weak_ptr to self for fail-safe shutdown detection
  auto weak_logging =
      std::weak_ptr<Logging>(std::static_pointer_cast<Logging>(shared_from_this()));

  scope.ExecuteOnContextThread(
      // NOLINTNEXTLINE(bugprone-exception-escape)
      [weak_logging, params](const CoreContext& context, const EventWriter& writer) {
        // Check if Logging object still alive
        auto logging = weak_logging.lock();
        if (!logging) {
          // Logging destroyed during shutdown, exit gracefully
          return;
        }

        // Safe to proceed - process the log event
        logging->ProcessLogEvent(params, context, writer);
      }
  );
}

void Logging::ProcessLogEvent(
    const LogCommandParams& params,
    const CoreContext& context,
    const EventWriter& writer
) const {
  // Phase C: Build event, enrich, encode, and write on context thread

  // Create a new LogEvent (no reuse from LoggerState)
  LogEvent ev(params.service, params.logger_name, _sdk_version, 8);

  // Populate basic fields from params
  ev.status = params.level;
  ev.date = params.timestamp;
  ev.message = params.message;

  // Enrich with RUM context if enabled
  if (params.enrichment.enable_rum && context.rum) {
    ev.rum_application_id = context.rum->application_id;
    ev.rum_session_id = context.rum->session_id;
    ev.rum_view_id = context.rum->view_id;
    ev.rum_action_id = context.rum->action_id;
  }

  // Enrich with OS properties
  if (context.os) {
    ev.os.value.emplace(
        context.os->name, context.os->version, context.os->version_major
    );
    if (!context.os->build.empty()) {
      ev.os.value->build = context.os->build;
    }
  }

  // Enrich with device properties
  if (context.device) {
    RumDeviceProperties& device = ev.device.value.emplace();
    if (!context.device->type.empty()) {
      DATADOG_ASSERT(
          context.device->type == "desktop",
          "DeviceInfo specifies non-desktop platform; log event serialization code "
          "must be updated"
      );
      device.type = RumDeviceType::Desktop;
    }
    device.name = context.device->name;
    device.model = context.device->model;
    device.brand = context.device->brand;
    device.architecture = context.device->architecture;
    device.locale = context.device->locale;
    device.time_zone = context.device->time_zone;
  }

  // Merge attributes with correct precedence: global < logger < message
  AttributeMerge::AssembleObject(
      ev.user_attributes,
      {params.global_attributes, params.logger_attributes, params.message_attributes}
  );

  // Encode to the shared buffer (accessed only on context thread)
  EncodeJson(_encode_buffer, ev);

  // Write the event
  writer(
      Block(
          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          reinterpret_cast<const char*>(_encode_buffer.data()),
          _encode_buffer.size()
      ),
      Block{}
  );
}

}  // namespace datadog::impl
