// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <mutex>
#include <string_view>

#include "datadog/core.hpp"

#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/profiling/profile_metadata.hpp"
#include "datadog/impl/profiling/profiler.hpp"
#include "datadog/impl/profiling/profiler_ownership.hpp"

namespace datadog::impl {

/**
 * Profiling feature implementation.
 *
 * Configures and starts the process-global profiler while the SDK is running and the
 * user has granted tracking consent, and stops it when the SDK stops or consent is
 * withdrawn. The profiler uploads profiles on its own, so it must not run unless
 * consent is granted: unlike other features, there is no way to hold its data back
 * while consent is pending. This feature neither writes events to storage nor produces
 * upload reports.
 *
 * While the profiler runs, the feature records the RUM events that RUM announces
 * (vitals and long tasks), so that `BuildProfileMetadata()` can describe them, along
 * with the application's identifying details, in the metadata of a profile. The events
 * are forgotten when the profiler stops.
 *
 * Tracking consent is learned from `ContextChangedMessage`s, which arrive on the
 * messaging thread, while `Start()` and `Stop()` are called from the main thread, so
 * all state is guarded by a mutex.
 */
class Profiling final : public Feature {
 public:
  /**
   * @param ownership The claim on the process-global profiler, held until this feature
   *  is destroyed. May be null only where no real profiler is involved, as in tests.
   */
  Profiling(
      std::unique_ptr<IProfiler> profiler,
      ProfilerSettings settings,
      std::unique_ptr<ProfilerOwnership> ownership
  );

  /**
   * Builds the metadata for a profile covering `range`, consuming the RUM events seen
   * since the previous call. Thread-safe.
   */
  ProfileMetadata BuildProfileMetadata(const ProfileTimeRange& range) {
    return _metadata_source.BuildMetadata(range);
  }

  // Feature interface
  FeatureId GetId() const override { return CreateFeatureId("PROF"); }

  std::string_view GetName() const override { return "profiling"; }

  std::optional<Report> UploadThread_PrepareReport(
      BatchReader& reader, HttpRequestBuilder& builder
  ) override;

  std::optional<std::function<void(const FeatureMessage&)>>
  MakeMessageHandler() override;

 protected:
  void Start() override;
  void Stop() override;

 private:
  /**
   * Record a RUM event for the metadata of the current profile. Events from before the
   * profiler started, or after it stopped, belong to no profile, and are ignored.
   */
  void OnRumVital(const RumVitalGeneratedMessage& message);
  void OnRumLongTask(const RumLongTaskGeneratedMessage& message);

  /** Handles a context update, reacting only if tracking consent has changed. */
  void OnTrackingConsent(TrackingConsent consent);

  /**
   * Starts or stops the profiler as needed to match the desired state: running if and
   * only if the SDK is running and consent is granted. Caller must hold _mutex.
   */
  void UpdateProfilerState();

  // Declared first so that it is released last, after the profiler has been stopped
  std::unique_ptr<ProfilerOwnership> _ownership;

  std::unique_ptr<IProfiler> _profiler;
  ProfilerSettings _settings;

  // Thread-safe, and guarded by no lock but its own
  ProfileMetadataSource _metadata_source;

  std::mutex _mutex;

  // True between Start() and Stop()
  bool _core_running = false;

  // Latest consent seen since Start(); not granted until a context update says so
  TrackingConsent _consent = TrackingConsent::Pending;

  // True if the profiler was successfully started, and so needs to be stopped
  bool _profiler_running = false;
};

}  // namespace datadog::impl
