// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "datadog/impl/core/executor.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/http/client.hpp"
#include "datadog/impl/core/http/request_builder.hpp"
#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/core/types.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

struct CoreContext;

/**
 * Global configuration details for the upload thread, to tune its behavior re: file age
 * cutoffs.
 */
struct UploadThreadConfig {
  /**
   * Until a file reaches this age, the upload thread will consider it still owned by
   * the storage thread and will refrain from reading or deleting it.
   */
  Duration min_file_age_for_read;

  /**
   * Once a file reacheds this age, the upload thread will refrain from processing it
   * for upload, and will instead delete it outright.
   *
   * TODO: This is meant to be configurable per-feature (so that RUM events can persist
   * on disk for 24h, rather than the default 18h); move it elsewhere.
   */
  Duration max_file_age_for_read{std::chrono::hours(18)};

  /**
   * Inclusive upper limit on the number of batch files that may be processed and
   * uploaded in a single upload cycle for a single feature.
   */
  size_t max_batches_per_cycle;

  explicit UploadThreadConfig(
      Duration in_min_file_age_for_read, size_t in_max_batches_per_cycle
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
  HttpRequestBuilder request_builder;

  Duration current_delay;
  Duration min_delay;
  Duration max_delay;

  /**
   * Initializes timing for a feature's upload cycles based on the configured frequency.
   */
  explicit UploadThreadState(const CoreContext& ctx, UploadFrequency upload_frequency);

  /**
   * Increases the delay in response to a failed upload, clamping at the configured
   * maximum.
   */
  Duration IncreaseDelayTowardMax();

  /**
   * Resets the delay to the minimum in response to a successful upload.
   */
  Duration ResetDelayToMin();
};

/**
 * Runs a complete upload cycle for the given feature on the calling thread, blocking
 * until it finishes, and returns the delay until the feature's next cycle.
 *
 * Used for the final flush in Core::Stop(), once the executor has shut down, and
 * exposed here to facilitate unit testing. UploadWorker runs the same steps, one batch
 * per queue item.
 */
Duration Internal_HandleUploadProc(
    DiagnosticLogger& diagnostic_logger,
    UploadThreadConfig config,
    const platform::IClock& clock,
    FeatureId feature_id,
    std::vector<struct RegisteredFeature>& features,
    class IFilesystem& fs,
    IHttpClient& http_client,
    std::vector<std::string>& mut_filenames,
    std::vector<char>& mut_read_buffer
);

/**
 * Runs upload cycles for all registered features on the executor's "upload" queue.
 * See description in `core.hpp`.
 *
 * Each feature's next cycle is scheduled with a delay that depends on the outcome of
 * its previous cycle (adaptive backoff). Cycles run one at a time, never interleaved,
 * because they share one HTTP client and one set of reusable buffers. Within a cycle,
 * each batch upload is a separate queue item, so that a cycle yields its worker thread
 * between HTTP requests when other work is waiting.
 *
 * The upload queue's stop policy is DiscardPending: closing it cancels pending cycles,
 * and abandons the current cycle after the HTTP request in flight, leaving unsent
 * batches on disk for a later run.
 */
class UploadWorker final {
 public:
  /**
   * @param features Non-owning reference to the array of registered features. The
   *  vector and its items must remain immutable, and outlive the UploadWorker.
   * @param http_client Non-owning reference to the HTTP client used for every upload;
   *  only ever used for one request at a time.
   */
  UploadWorker(
      Executor& executor,
      DiagnosticLogger& diagnostic_logger,
      UploadThreadConfig config,
      const platform::IClock& clock,
      std::vector<struct RegisteredFeature>& features,
      class IFilesystem& fs,
      IHttpClient& http_client
  );

  /** Closes the upload queue and waits for an in-progress request to finish. */
  ~UploadWorker();

  UploadWorker(const UploadWorker&) = delete;
  UploadWorker& operator=(const UploadWorker&) = delete;
  UploadWorker(UploadWorker&&) = delete;
  UploadWorker& operator=(UploadWorker&&) = delete;

  /** Schedules the first upload cycle for each feature. */
  void Start();

  /** Cancels all pending upload work, without blocking. */
  void Close();

  /** Blocks until no upload work is running. */
  void WaitIdle();

 private:
  // All of these run on the upload queue
  void ScheduleCycle(FeatureId feature_id, Duration delay);
  void OnCycleDue(FeatureId feature_id);
  void StartNextCycle();
  void ContinueCycle();
  void FinishCycle(Duration delay_until_next_cycle);

  DiagnosticLogger& _diagnostic_logger;
  const UploadThreadConfig _config;
  const platform::IClock& _clock;
  std::vector<struct RegisteredFeature>& _features;
  class IFilesystem& _fs;
  IHttpClient& _http_client;

  // Only accessed from items running on _queue, which never run concurrently
  std::vector<std::string> _filenames;
  std::vector<char> _read_buffer;
  std::deque<FeatureId> _due_features;
  std::unique_ptr<class UploadCycle> _cycle;
  FeatureId _cycle_feature_id{};

  // Declared last so that it's destroyed first, while the state above still exists
  std::unique_ptr<TaskQueue> _queue;
};

}  // namespace datadog::impl
