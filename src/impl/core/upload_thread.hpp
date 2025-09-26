// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

#include "core/feature.hpp"
#include "core/types.hpp"
#include "platform/clock.hpp"
#include "platform/http.hpp"

namespace datadog::impl {

/**
 * Global configuration details for the upload thread, to tune its behavior re: file age
 * cutoffs.
 */
struct UploadThreadConfig {
  /**
   * Until a file reaches this age, the upload thread will consider it still owned by
   * the storage thread and will refrain from reading or deleting it.
   */
  platform::Duration min_file_age_for_read;

  /**
   * Once a file reacheds this age, the upload thread will refrain from processing it
   * for upload, and will instead delete it outright.
   *
   * TODO: This is meant to be configurable per-feature (so that RUM events can persist
   * on disk for 24h, rather than the default 18h); move it elsewhere.
   */
  platform::Duration max_file_age_for_read{std::chrono::hours(18)};

  /**
   * Inclusive upper limit on the number of batch files that may be processed and
   * uploaded in a single upload cycle for a single feature.
   */
  size_t max_batches_per_cycle;

  explicit UploadThreadConfig(
      platform::Duration in_min_file_age_for_read, size_t in_max_batches_per_cycle
  );

  static UploadThreadConfig FromCoreConfig(
      BatchSize batch_size, BatchProcessingLevel batch_processing_level
  );
};

/**
 * Feature-specific state used by the upload thread to control the timing of upload
 * cycles.
 *
 * Implements adaptive backoff on a per-feature basis: if HTTP requests for a particular
 * feature's uploads fail continually, the time between upload attempts for that feature
 * will increase as a result.
 */
struct UploadThreadState {
  platform::Duration current_delay;
  platform::Duration min_delay;
  platform::Duration max_delay;

  /**
   * Initializes timing for a feature's upload cycles based on the configured frequency.
   */
  explicit UploadThreadState(UploadFrequency upload_frequency);

  /**
   * Increases the delay in response to a failed upload, clamping at the configured
   * maximum.
   */
  platform::Duration IncreaseDelayTowardMax();

  /**
   * Resets the delay to the minimum in response to a successful upload.
   */
  platform::Duration ResetDelayToMin();
};

/**
 * Initiates an upload cycle for the given feature.
 *
 * Called by UploadThreadMain when a feature is ready to be processed by the upload
 * thread. Exposed here to facilitate unit testing.
 */
platform::Duration Internal_HandleUploadProc(
    UploadThreadConfig config,
    const CoreContext& core_context,
    const platform::IClock& clock,
    FeatureId feature_id,
    std::vector<struct RegisteredFeature>& features,
    platform::IHttpClient& http_client,
    std::vector<std::string>& mut_filenames,
    std::vector<char>& mut_read_buffer
);

/**
 * Entry point for the upload thread. See description in `core.hpp`.
 *
 * @param config Global configuration values for the upload thread.
 * @param core_context The context values held by the SDK when the thread started;
 *  passed to UploadThread_PrepareReport to inform request headers etc. TODO: changes
 *  made via SetService() and SetEnv() do not propagate to this thread.
 * @param clock Interface to the system clock.
 * @param scheduler Non-owning reference to the object used to coordinate the timing of
 *  upload cycles for each registered feature. The main thread owns the scheduler and
 *  uses it to signal shutdown; but actual scheduling state is exclusive to the upload
 *  thread.
 * @param features Non-owning reference to the array of registered features. The vector
 *  and its items are guaranteed to remain immutable and to persist for the lifetime of
 *  the thread.
 * @param http_client Non-owning reference to the HTTP client that will be used to
 *  initiate requests for each report.
 */
void UploadThreadMain(
    UploadThreadConfig config,
    const CoreContext& core_context,
    const platform::IClock& clock,
    class UploadScheduler& scheduler,
    std::vector<struct RegisteredFeature>& features,
    platform::IHttpClient& http_client
);

}  // namespace datadog::impl
