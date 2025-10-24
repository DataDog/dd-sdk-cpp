// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "features/logging/logging.hpp"

#include <array>
#include <mutex>
#include <shared_mutex>
#include <string_view>

#include "attribute/merge.hpp"
#include "core/block.hpp"
#include "core/core.hpp"
#include "core/version.hpp"
#include "core/writer.hpp"
#include "date/date.h"
#include "features/logging/logger.hpp"
#include "json.hpp"

namespace datadog::impl {

static constexpr bool is_valid_user_attribute_name(std::string_view name) {
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

void Logging::SetAttribute(std::string_view name, const Attribute& value) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.SetObjectProperty(name, value);
}

void Logging::DeleteAttribute(std::string_view name) {
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
  // Grab a mutable reference to the object Attribute in which we'll build the log event
  Attribute& obj = mut_state.event_object.attribute;
  const LoggerState& state = mut_state;

  // Set the 'status' field based on the log level, using static Attribute strings
  obj.SetObjectProperty("status", Logging::GetLogLevelString(level));

  // Set 'service': use the logger-overridden value if set, otherwise use the global
  // default from SDK config
  std::string_view logger_service_name = state.service_name.attribute.GetStringValue();
  if (!logger_service_name.empty()) {
    obj.SetObjectProperty("service", state.service_name.attribute);
  } else {
    obj.SetObjectProperty("service", _default_service_name.attribute);
  }

  // Set 'date' from the current timestamp
  const Timestamp now = _clock.Now();
  obj.SetObjectProperty("date", Attribute::Timestamp(now));

  // Set 'message'
  // TODO: Reuse string memory on object property value set
  obj.SetObjectProperty("message", Attribute::String(message));

  // Set 'logger.name' to the name of the logger, if one was configured
  std::string_view logger_name = state.logger_name.attribute.GetStringValue();
  if (!logger_name.empty()) {
    obj.SetObjectProperty("logger.name", state.logger_name.attribute);
  }

  // Set 'logger.version' to the SDK version
  obj.SetObjectProperty("logger.version", _sdk_version.attribute);

  // If the logger is configured to enrich messages with context from other features,
  // obtain up-to-date values from the CoreContext, store them in LoggerState
  // attributes, and update the internal_attributes object
  if (enrichment.enable_rum && _scope) {
    // Get an immutable, thread-safe snapshot of the current context, and get RUM values
    const CoreContext context = _scope->GetContext();
    if (context.rum) {
      mut_state.rum_application_id.Set(context.rum->application_id);
      mut_state.rum_session_id.Set(context.rum->session_id);
      mut_state.rum_view_id.Set(context.rum->view_id);
      mut_state.rum_action_id.Set(context.rum->action_id);
    } else {
      mut_state.rum_application_id.Set(UUID::Zero);
      mut_state.rum_session_id.Set(UUID::Zero);
      mut_state.rum_view_id.Set(UUID::Zero);
      mut_state.rum_action_id.Set(UUID::Zero);
    }

    // Update the 'internal_attributes' object, which we'll merge into the final object
    Attribute& internal_obj = mut_state.internal_attributes.attribute;

    // Set 'application_id' from rum_application_id
    const Attribute& application_id = state.rum_application_id.attribute;
    if (application_id.GetUUIDValue() != UUID::Zero) {
      internal_obj.SetObjectProperty("application_id", application_id);
    } else {
      internal_obj.DeleteObjectProperty("application_id");
    }

    // Set 'session_id' from rum_session_id
    const Attribute& session_id = state.rum_session_id.attribute;
    if (session_id.GetUUIDValue() != UUID::Zero) {
      internal_obj.SetObjectProperty("session_id", session_id);
    } else {
      internal_obj.DeleteObjectProperty("session_id");
    }

    // Set 'view.id' from rum_view_id
    const Attribute& view_id = state.rum_view_id.attribute;
    if (view_id.GetUUIDValue() != UUID::Zero) {
      internal_obj.SetObjectProperty("view.id", view_id);
    } else {
      internal_obj.DeleteObjectProperty("view.id");
    }

    // Set 'user_action.id' from rum_action_id
    const Attribute& action_id = state.rum_action_id.attribute;
    if (action_id.GetUUIDValue() != UUID::Zero) {
      internal_obj.SetObjectProperty("user_action.id", action_id);
    } else {
      internal_obj.DeleteObjectProperty("user_action.id");
    }
  }

  // Merge our full set of Attribute::Object() values into a single object representing
  // the final JSON payload to be serialized. The destination object `mut_event_object`
  // already contains the essential set of log event attributes, and the filter function
  // `is_valid_user_attribute_name` prevents user attributes with conflicting names from
  // being merged in and clobbering those values. We merge in all user attributes,
  // followed by any internal attributes that inject context from other features, in
  // this order:
  // 1. The global attributes set via Logging::SetAttribute
  // 2. The logger-level attributes set via Logger::SetAttribute
  // 3. The message-level attributes supplied in the log call
  // 4. Internal attributes derived from other features' context values
  std::shared_lock read_only_lock(_global_attributes_mutex);
  AttributeMerge::AssembleObject(
      obj,
      {_global_attributes.attribute,
       state.user_attributes.attribute,
       message_attributes,
       state.internal_attributes.attribute},
      is_valid_user_attribute_name
  );
  read_only_lock.unlock();

  // Serialize to JSON, using the logger-owned buffer to ensure that it's safe to
  // serialize multiple messages from different loggers concurrently
  EncodeJson(mut_state.event_buffer, obj);

  WriteEvent(Block(
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const char*>(mut_state.event_buffer.data()),
      mut_state.event_buffer.size()
  ));
}

Attribute Logging::GetLogLevelString(LogLevel level) {
  // Establish a set of read-only string attributes for each of our log level names,
  // with static storage
  static Attribute debug = Attribute::String(LogLevel_ToString(LogLevel::Debug));
  static Attribute info = Attribute::String(LogLevel_ToString(LogLevel::Info));
  static Attribute notice = Attribute::String(LogLevel_ToString(LogLevel::Notice));
  static Attribute warn = Attribute::String(LogLevel_ToString(LogLevel::Warn));
  static Attribute error = Attribute::String(LogLevel_ToString(LogLevel::Error));
  static Attribute critical = Attribute::String(LogLevel_ToString(LogLevel::Critical));

  // Return the appropriate attribute value for the given log level
  switch (level) {
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

}  // namespace datadog::impl
