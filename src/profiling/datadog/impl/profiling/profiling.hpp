// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>

#include "datadog/core.hpp"

#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/profiling/profile_metadata.hpp"
#include "datadog/impl/profiling/profiler_backend.hpp"

namespace datadog::impl {

class Core;

/**
 * Profiling feature implementation.
 *
 * Configures and starts the process-global profiler while the SDK is running, unless
 * the user has refused tracking consent, and stops it when the SDK stops or consent is
 * refused. While consent is pending the profiler runs, and its profiles are stored like
 * the events of any other feature: the storage layer holds them until consent is
 * granted, when they become available for upload, and deletes them if consent is
 * refused.
 *
 * The profiling backend hands each finished profile to this feature, which stores it,
 * with metadata describing it, as one event in storage. The Core's upload thread then
 * asks this feature to prepare the request for each stored profile, which is a
 * `multipart/form-data` POST to the profiling intake with the event, the profile, and
 * the RUM events that it is correlated with.
 *
 * While the feature is active and the profiler is running, it records the RUM events
 * that RUM announces (vitals and long tasks), along with the application's identifying
 * details, in the metadata of a profile. The events are forgotten when the profiler
 * stops. As in the other native SDKs, a profile for which no events were recorded has
 * nothing to correlate it with, and is dropped.
 */
class Profiling final : public Feature {
 public:
  /**
   * The tracking consent that decides whether the profiler runs is read from the Core
   * each time the feature starts, so that a change made while the Core was stopped is
   * honored from the start of the run. Each context update then keeps it current.
   */
  Profiling(
      std::unique_ptr<IProfilerBackend> profiler, ProfilerBackendSettings settings
  );

  /**
   * Creates the Profiling feature for `core` and registers it with that core. This is
   * the shared body of the C and C++ APIs' registration functions.
   *
   * Returns null, after logging the reason, if this build has no profiler, if another
   * Core already holds the process-global profiler, or if registration fails.
   */
  static std::shared_ptr<Profiling> CreateAndRegister(
      Core& core, const DiagnosticLogger& logger
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

  /**
   * Each profile is stored in a batch file of its own, since the profiling intake takes
   * one profile per request, and the size limits allow for a large profile.
   */
  FeatureStorageConfig GetStorageConfig() const override;

  /**
   * Prepares the upload of the profile in the batch file, as a `multipart/form-data`
   * request to the profiling intake whose body is streamed from the stored profile
   * without copying it. The body is not compressed, like the requests of other
   * features; the profiler has already compressed the profile.
   *
   * Returns nothing, so that the Core treats the batch as unusable and deletes it, if
   * the file can't be read, has no event, or has an event that is not a stored profile.
   * Called on the upload thread. It reads no state of this feature.
   */
  std::optional<Report> UploadThread_PrepareReport(
      BatchReader& reader, HttpRequestBuilder& builder
  ) override;

  std::optional<std::function<void(const FeatureMessage&)>>
  MakeMessageHandler() override;

 protected:
  void Start() override;
  void PrepareToStop() override;
  void Stop() override;

 private:
  /**
   * Stores a profile that the profiler has produced, with metadata that describes it,
   * as an event.
   *
   * If the profiler fails to produce a profile, or if it is empty, or if it is too
   * large, this function drops the profile and logs why, before forgetting the metadata
   * that would have been associated with the profile.
   *
   * A profile is also dropped if no RUM events, such as vitals or long tasks, were
   * recorded while it was collected, since there is nothing to correlate it with, or if
   * the profiler delivers it as it stops because consent was refused.
   *
   * `profile` borrows the profiler's bytes, which are valid only until this returns, so
   * a profile that is stored is copied into its frame before then.
   *
   * Called on the profiler's thread, or on the thread that stops the profiler. It reads
   * `_scope` without holding `_mutex`, which is safe because the profiler is always
   * stopped, and its last in-flight call to this function has returned, before
   * `Feature::OnCoreStopping()` resets `_scope`. Stopping the profiler waits for that
   * call, so the profiler must not be changed to return from Stop() with one pending.
   */
  void OnProfile(const ProfileView& profile);

  /**
   * Record a RUM event for the metadata of the current profile. Events from before the
   * profiler started, or after it stopped, belong to no profile, and are ignored.
   */
  void OnRumVital(const RumVitalGeneratedMessage& message);
  void OnRumLongTask(const RumLongTaskGeneratedMessage& message);

  /** Handles a context update, reacting only if tracking consent has changed. */
  void OnTrackingConsentChanged(TrackingConsent consent);

  /**
   * Starts or stops the profiler as needed to match the desired state: running if and
   * only if the SDK is running and not stopping, and consent is not
   * TrackingConsent::NotGranted (it may be pending). Each time it starts the profiler,
   * it first gives it the function that receives its profiles. Caller must hold _mutex.
   */
  void UpdateBackendState();

  std::unique_ptr<IProfilerBackend> _backend;
  ProfilerBackendSettings _settings;

  // Thread-safe, and guarded by no lock but its own
  ProfileMetadataSource _metadata_source;

  std::mutex _mutex;

  // The latest tracking consent: the Core's, read when the feature starts, then updated
  // by each context update. It is meaningful only between Start() and Stop().
  TrackingConsent _consent = TrackingConsent::Pending;

  // True if the profiler was successfully started, and so needs to be stopped
  bool _backend_running = false;

  // True once the SDK is shutting down, until the next Start(): the profiler has been
  // stopped, and a context update that grants consent must not restart it.
  bool _stopping = false;

  // True while the profiler is being stopped because consent was refused, until it next
  // starts: the last profile, which it delivers as it stops, is dropped. Atomic because
  // OnProfile() reads it without holding _mutex.
  std::atomic<bool> _discard_profiles = false;
};

}  // namespace datadog::impl
