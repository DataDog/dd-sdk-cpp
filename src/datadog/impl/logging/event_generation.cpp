// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/logging/event_generation.hpp"

#include <string_view>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/feature_types/logging.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/upload_util.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/json.hpp"
#include "datadog/impl/logging/data.hpp"
#include "datadog/impl/rum/scopes/event_enrichment.hpp"

namespace datadog::impl {

void ContextThread_GenerateLogEvent(
    const LoggerConfigDetails& logger,
    LogCallDetails call,
    const CoreContext& ctx,
    const EventWriter& event_writer,
    std::vector<uint8_t>& encode_buf,
    const MessagePublisher& publisher,
    const DiagnosticLogger& diagnostic_logger
) {
  // Use the overridden service name configured for this Logger if we have one;
  // otherwise fall back to the service name from SDK config
  std::string_view service_name = logger.service_override;
  if (service_name.empty()) {
    service_name = ctx.service;
  }

  // If the logger has no service override and no custom tags, we can use the immutable
  // ddtags value provided via CoreContext
  std::string_view ddtags = ctx.per_event_ddtags;

  // Otherwise, we need to build a fresh ddtags string that incorporates all the
  // relevant details from `CoreContext`, plus any service override or additional tags
  // configured for this logger
  std::string override_ddtags;
  if (!logger.service_override.empty() || !call.logger_tags.empty()) {
    override_ddtags = BuildDdTags(
        logger.service_override.empty() ? ctx.service : logger.service_override,
        ctx.application_version,
        ctx.env,
        ctx.sdk_version,
        ctx.variant,
        call.logger_tags
    );
    ddtags = override_ddtags;
  }

  // Construct a LogEvent with all required values, moving our original copy of the
  // application-provided message into the event struct
  LogEvent ev{
      call.level,
      service_name,
      call.timestamp,
      std::move(call.message),
      ddtags,
      logger.name,
      ctx.sdk_version
  };

  // If configured to do so, add RUM IDs for any active session/view/action
  if (logger.enrich_with_rum_context && ctx.rum.has_value()) {
    // These fields are OmitIfZero<UUID>, so if no session/view/action is currently
    // active, the corresponding field will be omitted from the event as intended
    ev.rum_application_id = ctx.rum->application_id;
    ev.rum_session_id = ctx.rum->session_id;
    ev.rum_view_id = ctx.rum->view_id;
    ev.rum_action_id = ctx.rum->action_id;
  }

  // If the SDK has been given valid user info, or has an anonymous id to expose, add
  // 'usr'
  if (!ctx.user_info.IsEmpty() || ctx.anonymous_id_enabled) {
    LogUserInfo& usr = ev.usr.value.emplace();
    usr.id = ctx.user_info.id;
    usr.name = ctx.user_info.name;
    usr.email = ctx.user_info.email;
    usr.extra = ctx.user_info.extra;
    if (ctx.anonymous_id_enabled) {
      usr.anonymous_id = ctx.anonymous_id;
    }
  }

  // If the SDK has been given valid account info, add it to 'account'
  if (!ctx.account_info.IsEmpty()) {
    LogAccountInfo& account = ev.account.value.emplace();
    account.id = ctx.account_info.id;
    account.name = ctx.account_info.name;
    account.extra = ctx.account_info.extra;
  }

  // Add 'os' properties from OsInfo
  if (ctx.os) {
    RumOSProperties& os =
        ev.os.value.emplace(ctx.os->name, ctx.os->version, ctx.os->version_major);
    if (!ctx.os->build.empty()) {
      os.build = ctx.os->build;
    }
  }

  // Add 'device' properties from DeviceInfo
  if (ctx.device) {
    RumDeviceProperties& device = ev.device.value.emplace();
    if (!ctx.device->type.empty()) {
      // Currently, ISystemInfo only produces a value of "desktop"; if it uses other
      // values we'll need to actually parse to RumDeviceType here (or use an enum in
      // DeviceInfo)
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

  // Add any error details provided with this call
  ev.error_message = std::move(call.error_message);
  ev.error_kind = std::move(call.error_kind);
  ev.error_stack = std::move(call.error_stack);

  // Add extra user attributes, to be merged into the event payload as top-level JSON
  // properties. Keep a cheap copy-on-write copy of the un-extracted attributes to
  // forward to RUM below: extracting `_dd.error.source_type` for this LogEvent below
  // deletes the key from `ev.user_attributes`, but the Logger->RUM bridge (triggered
  // for Error/Critical logs) needs to see the original attribute to run its own
  // independent extraction against the resulting RUM error event.
  Attribute unextracted_attributes = call.merged_attributes;
  ev.user_attributes = std::move(call.merged_attributes);

  // Extract a `_dd.error.source_type` override, if supplied alongside this log call,
  // and strip it from the attributes that end up as this event's custom attributes.
  if (std::optional<RumErrorSourceType> source_type = ExtractErrorSourceType(
          {ev.user_attributes}, ev.user_attributes, diagnostic_logger
      )) {
    ev.error_source_type = std::string(StringRumErrorSourceType(*source_type).Name());
  }

  // Extract a `_dd.error.fingerprint` override, if supplied alongside this log call,
  // and strip it from the attributes that end up as this event's custom attributes.
  if (std::optional<std::string> fingerprint =
          ExtractErrorFingerprint(ev.user_attributes, diagnostic_logger)) {
    ev.error_fingerprint = std::move(*fingerprint);
  }

  // Encode to the shared buffer (accessed only on context thread)
  EncodeJson(encode_buf, ev);
  std::string_view data{
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const char*>(encode_buf.data()),
      encode_buf.size()
  };

  // Enqueue the event for storage
  const bool bypass_tracking_consent = false;
  event_writer(data, {}, bypass_tracking_consent);

  // After writing the log event, forward it to RUM as an error event: LogEvent owns
  // strings and attributes that will simply go out of scope after this call, so we can
  // transfer ownership of those values into the message
  if (call.level >= LogLevel::Error) {
    publisher(
        LogErrorGeneratedMessage{
            call.timestamp,
            std::move(ev.error_message.value),
            std::move(ev.error_kind.value),
            std::move(ev.error_stack.value),
            std::move(unextracted_attributes),
        }
    );
  }
}

}  // namespace datadog::impl
