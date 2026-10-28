// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profiling.hpp"

#include <utility>
#include <variant>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

Profiling::Profiling(std::unique_ptr<IProfiler> profiler, ProfilerSettings settings)
    : _profiler(std::move(profiler)), _settings(std::move(settings)) {
  DATADOG_ASSERT(_profiler, "Profiling feature requires a profiler");
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
      self->OnTrackingConsentChanged(m->context.tracking_consent);
    }
  };
}

void Profiling::Start() {
  std::lock_guard<std::mutex> lock(_mutex);
  _core_running = true;
  UpdateProfilerState();
}

void Profiling::Stop() {
  std::lock_guard<std::mutex> lock(_mutex);
  _core_running = false;

  // Forget consent, so that the next run waits for its own context update before
  // starting the profiler
  _consent = TrackingConsent::Pending;
  UpdateProfilerState();
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
  UpdateProfilerState();
}

void Profiling::UpdateProfilerState() {
  const bool should_run = _core_running && _consent == TrackingConsent::Granted;

  if (should_run && !_profiler_running) {
    // _core_running is only set between Start() and Stop(), so the scope is always
    // valid here
    if (!_scope) {
      DATADOG_ASSERT(false, "Profiling has invalid _scope while the SDK is running");
      return;
    }
    if (!_profiler->Init(_settings)) {
      _scope->diagnostic_logger.Warning("Failed to configure profiler");
      return;
    }
    if (!_profiler->Start()) {
      _scope->diagnostic_logger.Warning("Failed to start profiler");
      return;
    }
    _profiler_running = true;
  } else if (!should_run && _profiler_running) {
    _profiler->Stop();
    _profiler_running = false;
  }
}

}  // namespace datadog::impl
