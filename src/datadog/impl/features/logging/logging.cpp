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
) const {
  // Grab a mutable reference to the LogEvent that represents our payload: each Logger
  // holds its own mutable LogEvent struct so that multiple OnLoggerEmit calls can build
  // and generate log events concurrently, so long as each comes from a different Logger
  LogEvent& ev = mut_state.event;
  const LoggerState& state = mut_state;

  // Clear the subset of LogEvent members that may change between events but which are
  // not unconditionally set
  ev.Reset();

  // Update basic log event properties
  ev.status = level;
  if (ev.service.empty()) {
    ev.service = _default_service_name;
  }
  ev.date = _clock.Now();
  ev.message = message;

  // Read CoreContext if available, so we can enrich events with additional SDK state
  if (_scope) {
    // Obtain a read-only copy of the context
    const CoreContext ctx = _scope->GetContext();

    // If we're configured to enrich log events with RUM context, write all RUM IDs into
    // the event (UUID::Zero values will be omitted from the JSON payload)
    if (enrichment.enable_rum && ctx.rum) {
      ev.rum_application_id = ctx.rum->application_id;
      ev.rum_session_id = ctx.rum->session_id;
      ev.rum_view_id = ctx.rum->view_id;
      ev.rum_action_id = ctx.rum->action_id;
    }

    // If we have OS properties, add them to the event payload
    if (ctx.os) {
      ev.os.value.emplace(ctx.os->name, ctx.os->version, ctx.os->version_major);
      if (!ctx.os->build.empty()) {
        ev.os.value->build = ctx.os->build;
      }
    }

    // If we have device properties, add them to the event payload
    if (ctx.device) {
      RumDeviceProperties& device = ev.device.value.emplace();
      if (!ctx.device->type.empty()) {
        DATADOG_ASSERT(
            ctx.device->type == "desktop",
            "DeviceInfo specifies non-desktop platform; log event serialization code "
            "must be updated"
        );
        device.type = RumDeviceType::Desktop;
      }
      device.name = ctx.device->name;
      device.model = ctx.device->model;
      device.brand = ctx.device->brand;
      device.architecture = ctx.device->architecture;
      device.locale = ctx.device->locale;
      device.time_zone = ctx.device->time_zone;
    }
  }

  // Create a shallow copy of the Logging feature's global attributes, so we don't need
  // to hold a lock during event serialization
  Attribute global_attributes;
  {
    std::shared_lock read_lock(_global_attributes_mutex);
    global_attributes = _global_attributes.attribute;
  }

  // Merge the full set of custom, user-specified attribute values into the LogEvent
  // payload's 'user_attributes' member. User attributes are merged into the event at
  // top-level, with the following order:
  //
  // 1. The global attributes set via Logging::AddAttribute
  // 2. The logger-level attributes set via Logger::AddAttribute
  // 3. The message-level attributes supplied in the log call
  //
  // If multiple user attributes share the same property name, the value that appears
  // later in the list will take precedence.
  AttributeMerge::AssembleObject(
      mut_state.event.user_attributes,
      {global_attributes, state.user_attributes.attribute, message_attributes}
  );

  // Now that we've fully populated our LogEvent value, serialize it as a JSON object,
  // with all custom attributes from 'user_attributes' merged in at top level. If any
  // properties in user_attributes use names that are already used by LogEvent struct
  // fields, those custom attribute values will be ignored.
  EncodeJson(mut_state.event_buffer, state.event);

  // Create a view of the JSON payload now held in our buffer, and call WriteEvent,
  // which will copy our event data onto the storage queue
  WriteEvent(Block(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const char*>(mut_state.event_buffer.data()),
      mut_state.event_buffer.size()
  ));
}

}  // namespace datadog::impl
