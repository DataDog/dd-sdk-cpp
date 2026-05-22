// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/logging/event_generation.hpp"

#include <string_view>

#include "datadog/impl/core/feature_types/logging.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/upload_util.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/json.hpp"
#include "datadog/impl/logging/data.hpp"

namespace datadog::impl {

void ContextThread_GenerateLogEvent(
    const LoggerConfigDetails& logger,
    LogCallDetails call,
    const CoreContext& ctx,
    const EventWriter& event_writer,
    std::vector<uint8_t>& encode_buf
) {
  // Use the overridden service name configured for this Logger if we have one;
  // otherwise fall back to the service name from SDK config
  std::string_view service_name = logger.service_override;
  if (service_name.empty()) {
    service_name = ctx.service;
  }

  // When the logger has a service override, build a fresh ddtags string using that
  // service so that `ddtags` is consistent with the `service` field on the event
  std::string override_ddtags;
  std::string_view ddtags = ctx.per_event_ddtags;
  if (!logger.service_override.empty()) {
    override_ddtags = BuildDdTags(
        logger.service_override,
        ctx.application_version,
        ctx.env,
        ctx.sdk_version,
        ctx.variant
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

  // If the SDK has been given valid user info, add it to 'usr'
  if (!ctx.user_info.IsEmpty()) {
    LogUserInfo& usr = ev.usr.value.emplace();
    usr.id = ctx.user_info.id;
    usr.name = ctx.user_info.name;
    usr.email = ctx.user_info.email;
    usr.extra = ctx.user_info.extra;
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

  // Add extra user attributes, to be merged into the event payload as top-level JSON
  // properties
  ev.user_attributes = std::move(call.merged_attributes);

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
}

}  // namespace datadog::impl
