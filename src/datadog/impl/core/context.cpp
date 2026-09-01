// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/context.hpp"

#include <mutex>
#include <shared_mutex>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/message_bus.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/upload_util.hpp"
#include "datadog/impl/core/version.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

static std::string_view _override_or(
    std::string_view override_value, std::string_view default_value
) {
  if (!override_value.empty()) {
    return override_value;
  }
  return default_value;
}

ImmutableContext::ImmutableContext(
    const CoreConfig& config,
    const platform::OsInfo& os_info,
    const platform::DeviceInfo& device_info,
    Timestamp in_process_launch_time,
    std::string_view http_subsystem_name,
    std::string_view http_subsystem_version
)
    : os(&os_info),
      device(&device_info),
      process_launch_time(in_process_launch_time),
      client_token(config.client_token),
      service(config.service),
      env(config.env),
      application_version(config.application_version),
      variant(config.variant),
      source(_override_or(config.internal_options.source, "cpp")),
      sdk_version(_override_or(config.internal_options.sdk_version, SDK_VERSION)),
      intake_origin(
          GetIntakeOrigin(config.site, config.internal_options.custom_endpoint_url)
      ),
      user_agent(GetUserAgent(
          service,
          application_version,
          http_subsystem_name,
          http_subsystem_version,
          device->name,
          os->name,
          os->version
      )),
      per_event_ddtags(
          BuildDdTags(service, application_version, env, sdk_version, config.variant)
      ) {}

CoreContext::CoreContext(
    const ImmutableContext& im, TrackingConsent initial_tracking_consent
)
    : os(im.os),
      device(im.device),
      process_launch_time(im.process_launch_time),
      client_token(im.client_token),
      service(im.service),
      env(im.env),
      application_version(im.application_version),
      variant(im.variant),
      source(im.source),
      sdk_version(im.sdk_version),
      intake_origin(im.intake_origin),
      user_agent(im.user_agent),
      per_event_ddtags(im.per_event_ddtags),
      tracking_consent(initial_tracking_consent) {}

void CoreContext::Reset() { rum.reset(); }

CoreContextProvider::CoreContextProvider(const CoreContext& context)
    : _context(context) {}

CoreContext CoreContextProvider::Get() const {
  // Acquire a read-only lock, then create and return a copy of the context
  std::shared_lock lock(_mutex);
  return _context;
}

void CoreContextProvider::Update(const std::function<void(CoreContext&)>& callback) {
  // Acquire an exclusive write lock, mutate the context, and capture a snapshot. The
  // lock is released before Send() to minimize contention on the message bus.
  CoreContext snapshot = [&] {
    std::unique_lock lock(_mutex);
    callback(_context);
    return _context;
  }();

  // CoreContext has been mutated (and we have a valid snapshot): notify all registered
  // message-handlers of the change, so that Features can perform work (on the messaging
  // thread) in response to the updated context
  if (_message_bus) {
    _message_bus->Send(ContextChangedMessage{std::move(snapshot)});
  }
}

void CoreContextProvider::SetMessageBus(MessageBus* bus) { _message_bus = bus; }

}  // namespace datadog::impl
