// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profiling.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/profiling/profile_frame.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

Profiling::Profiling(
    std::unique_ptr<IProfilerBackend> profiler, ProfilerBackendSettings settings
)
    : _backend(std::move(profiler)), _settings(std::move(settings)) {
  DATADOG_ASSERT(_backend, "Profiling feature requires a profiler");
}

std::shared_ptr<Profiling> Profiling::CreateAndRegister(
    Core& core, const DiagnosticLogger& logger
) {
  // The profiler is process-global: this returns null (after logging why) if this
  // build has no profiler, or if another SDK instance is already using it
  auto profiler = CreateProfilerBackend(logger);
  if (!profiler) {
    return nullptr;
  }

  const ImmutableContext& im = core.GetImmutableContext();
  ProfilerBackendSettings settings;
  settings.service = im.service;
  settings.env = im.env;
  settings.version = im.application_version;

  auto profiling =
      std::make_shared<Profiling>(std::move(profiler), std::move(settings));
  if (!core.RegisterFeature(profiling)) {
    return nullptr;
  }
  return profiling;
}

// The largest profile that is stored: a larger one is dropped. This is consistent with
// the other native SDKs.
static constexpr size_t MAX_PROFILE_SIZE = static_cast<size_t>(10) * 1024 * 1024;

// The most that a stored profile may take, counting the metadata stored with it: 5 MB
// more than the largest profile, which is also consistent with the other native SDKs.
static constexpr size_t MAX_STORED_PROFILE_SIZE =
    MAX_PROFILE_SIZE + (static_cast<size_t>(5) * 1024 * 1024);

FeatureStorageConfig Profiling::GetStorageConfig() const {
  FeatureStorageConfig config;
  config.max_writes_per_file = 1;
  config.max_event_size = MAX_STORED_PROFILE_SIZE;
  config.max_file_size = MAX_STORED_PROFILE_SIZE;
  return config;
}

std::optional<Report> Profiling::UploadThread_PrepareReport(
    BatchReader& reader, HttpRequestBuilder& builder
) {
  // Uploading stored profiles is not implemented yet, so there is nothing to report
  (void)reader;
  (void)builder;
  return std::nullopt;
}

std::optional<std::function<void(const FeatureMessage&)>>
Profiling::MakeMessageHandler() {
  // Bind a weak_ptr to this so the callback will silently no-op after we're destroyed
  const auto weak_self = weak_from_this();
  return [weak_self](const FeatureMessage& msg) {
    auto self = std::static_pointer_cast<Profiling>(weak_self.lock());
    if (!self) {
      return;
    }
    if (const auto* m = std::get_if<ContextChangedMessage>(&msg)) {
      self->_metadata_source.OnContextChanged(m->context);
      self->OnTrackingConsentChanged(m->context.tracking_consent);
    } else if (const auto* vital = std::get_if<RumVitalGeneratedMessage>(&msg)) {
      self->OnRumVital(*vital);
    } else if (const auto* long_task = std::get_if<RumLongTaskGeneratedMessage>(&msg)) {
      self->OnRumLongTask(*long_task);
    }
  };
}

void Profiling::Start() {
  std::lock_guard<std::mutex> lock(_mutex);

  // We should always have a valid FeatureScope on Start
  if (!_scope) {
    DATADOG_ASSERT(_scope, "Profiling has invalid _scope on Start()");
    return;
  }
  _stopping = false;

  // Take the consent that the Core has now, rather than the one we last saw: it may
  // have changed while the Core was stopped, or before it first started, and no context
  // update is delivered to us for a change made then. Without this, the profiler would
  // run until the run's first context update arrived, even though consent was refused.
  _consent = _scope->GetContext().tracking_consent;

  UpdateBackendState();
}

void Profiling::PrepareToStop() {
  std::lock_guard<std::mutex> lock(_mutex);

  // Stop the profiler now, rather than in Stop(): it delivers a last profile as it
  // stops, and only while the context thread is still running can the Core store it.
  // Messages can still arrive until the Core stops its message bus, so make sure that
  // none of them restarts the profiler. A vital or long task that RUM announced just
  // before this, but that the messaging thread has not yet delivered, is missed by the
  // last profile, which is dropped if it then has no RUM events at all.
  _stopping = true;
  UpdateBackendState();
}

void Profiling::Stop() {
  std::lock_guard<std::mutex> lock(_mutex);
  // The profiler has normally been stopped already, by PrepareToStop(): make sure. The
  // consent is kept for the next run, which learns of any change from its first context
  // update.
  _stopping = true;
  UpdateBackendState();
}

void Profiling::OnProfile(const ProfileView& profile) {
  if (!_scope) {
    // The SDK is not running, so there is nowhere to store the profile; drop it
    return;
  }

  FeatureScope& scope = *_scope;
  const DiagnosticLogger& logger = scope.diagnostic_logger;

  // Drop a profile that the profiler failed to produce, that has no data, or that is
  // too large to store. Forget any RUM events that were collected so that they don't
  // mistakenly get associated to the next profile.
  if (!profile.error.empty() || profile.pprof.empty() ||
      profile.pprof.size() > MAX_PROFILE_SIZE) {
    logger.Debug(
        "Dropped a profile because it was missing, empty, or too large; profiling "
        "continues",
        {{"error", profile.error},
         {"profile_size", profile.pprof.size()},
         {"max_profile_size", MAX_PROFILE_SIZE}}
    );
    _metadata_source.ResetRumEvents();
    return;
  }

  // Store everything that the upload needs as a single event: the profile, with the
  // metadata that describes it and the RUM events seen while it was collected
  const ProfileMetadata metadata =
      BuildProfileMetadata(ProfileTimeRange{profile.start, profile.end});

  // Like the other native SDKs, keep a profile only if there is a RUM event, such as a
  // vital or a long task, to correlate it with. Building the metadata has already taken
  // the events there were, so there is nothing to forget.
  if (!metadata.has_rum_events) {
    logger.Debug(
        "Dropped a profile because there were no RUM events to correlate it with; "
        "profiling continues",
        {{"profile_size", profile.pprof.size()}}
    );
    return;
  }

  std::vector<uint8_t> frame = EncodeProfileFrame(metadata, profile.pprof);
  if (frame.empty()) {
    // A part is too large for the frame's size field; give up on this profile, and keep
    // profiling
    logger.Debug(
        "Dropped a profile because it could not be encoded; profiling continues"
    );
    return;
  }

  // The profile's bytes are only valid until this callback returns, so the frame is
  // their one copy, which EncodeProfileFrame() made. Move it into the function we hand
  // to the context thread, from which the scope moves it on, and WriteMove() hands it
  // to the storage queue, so that this large profile is not copied again. The logger is
  // copied into the function, as it runs later, after this function has returned.
  scope.ExecuteOnContextThread(
      [frame = std::move(frame), logger](
          const CoreContext&, const EventWriter& writer, const MessagePublisher&
      ) mutable {
        const bool bypass_tracking_consent = false;
        if (!writer.WriteMove(std::move(frame), {}, bypass_tracking_consent)) {
          logger.Debug(
              "Dropped a profile because it could not be stored, which can happen as "
              "the SDK shuts down"
          );
        }
      }
  );
}

void Profiling::OnRumVital(const RumVitalGeneratedMessage& message) {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_backend_running) {
    _metadata_source.OnVital(message);
  }
}

void Profiling::OnRumLongTask(const RumLongTaskGeneratedMessage& message) {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_backend_running) {
    _metadata_source.OnLongTask(message);
  }
}

void Profiling::OnTrackingConsentChanged(TrackingConsent consent) {
  std::lock_guard<std::mutex> lock(_mutex);

  // Every context change (user info, etc.) arrives here: only a consent change can
  // alter whether the profiler should run, and ignoring the rest avoids retrying, and
  // logging again, after a failed start
  if (consent == _consent) {
    return;
  }
  _consent = consent;
  UpdateBackendState();
}

void Profiling::UpdateBackendState() {
  // The profiler runs unless the user has refused consent: while consent is pending,
  // its profiles are stored like any other feature's events, and held by the storage
  // layer until consent is granted, or deleted if it is refused.
  const bool should_run =
      _scope.has_value() && !_stopping && _consent != TrackingConsent::NotGranted;

  if (should_run && !_backend_running) {
    // The profiler hands us each profile that it produces. Give it the function that
    // does so before it is configured, so that dd-win-prof selects callback delivery
    // and does not require HTTP credentials: it must not be running yet. It drops the
    // function whenever it stops, so this is done for every run. A weak_ptr to this
    // lets the function silently do nothing if we have been destroyed.
    const auto weak_self = weak_from_this();
    if (!_backend->SetProfileCallback([weak_self](const ProfileView& profile) {
          auto self = std::static_pointer_cast<Profiling>(weak_self.lock());
          if (self) {
            self->OnProfile(profile);
          }
        })) {
      _scope->diagnostic_logger.Warning(
          "Profiling is not running: the profiler could not be set up to deliver "
          "profiles to the SDK, so no profiles will be collected or uploaded"
      );
      // Without callback delivery, dd-win-prof would fall back to HTTP upload; don't
      // start.
      return;
    }
    if (!_backend->Init(_settings)) {
      _scope->diagnostic_logger.Warning(
          "Profiling is not running: the profiler could not be configured, so no "
          "profiles will be collected or uploaded"
      );
      // The backend unregistered its callback; profiling stays off until consent
      // changes.
      return;
    }
    if (!_backend->Start()) {
      _scope->diagnostic_logger.Warning(
          "Profiling is not running: the profiler could not be started, so no profiles "
          "will be collected or uploaded"
      );
      // The backend stopped and unregistered; profiling stays off until consent
      // changes.
      return;
    }
    _backend_running = true;
  } else if (!should_run && _backend_running) {
    _backend->Stop();
    _backend_running = false;
    _metadata_source.ResetRumEvents();
  }
}

}  // namespace datadog::impl
