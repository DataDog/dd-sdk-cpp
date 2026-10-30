// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profiling.hpp"

#include <utility>
#include <variant>

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature_message.hpp"
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

std::optional<Report> Profiling::UploadThread_PrepareReport(
    BatchReader& reader, HttpRequestBuilder& builder
) {
  // The profiler uploads profiles on its own, so there are no stored events to report
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
  UpdateBackendState();
}

void Profiling::Stop() {
  std::lock_guard<std::mutex> lock(_mutex);
  // Reset consent. This stops the profiler (via UpdateBackendState() below), and
  // makes the next run wait for its own context update before starting the profiler.
  _consent = TrackingConsent::Pending;
  UpdateBackendState();
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
  const bool should_run = _scope.has_value() && _consent == TrackingConsent::Granted;

  if (should_run && !_backend_running) {
    if (!_backend->Init(_settings)) {
      _scope->diagnostic_logger.Warning("Failed to configure profiler");
      return;
    }
    if (!_backend->Start()) {
      _scope->diagnostic_logger.Warning("Failed to start profiler");
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
