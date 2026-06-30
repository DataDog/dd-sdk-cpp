// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/upload_thread.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <iostream>
#include <queue>
#include <string>
#include <thread>

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature_read.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/core/storage/util.hpp"
#include "datadog/impl/core/upload_scheduler.hpp"
#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

static constexpr Duration from_seconds(double sec) {
  return std::chrono::duration_cast<Duration>(std::chrono::duration<double>(sec));
}

static const Duration ASSERTION_FAILURE_BACKOFF = from_seconds(60.0);

enum class _process_and_upload_batch_result : uint8_t {
  /**
   * Request was successful; upload thread should delete the file and continue the
   * current upload cycle.
   */
  success,
  /**
   * Request failed due to transient network conditions that may improve in the future;
   * upload thread should retain the file, abort the current upload cycle, and increase
   * backoff delay.
   */
  retryable_failure,
  /**
   * Request failed because the batch itself is malformed, the feature implementation
   * was unable to process it, the resulting HTTP request was malformed, or the remote
   * server rejected the batch; upload thread should delete the file and continue the
   * current upload cycle.
   */
  bad_batch
};

static _process_and_upload_batch_result _interpret_http_result(const HttpResult res) {
  switch (res.type) {
    // If we couldn't even attempt the request, or if we failed to get a response for a
    // reason that indicates an inherent problem with the request, rather than transient
    // network conditions etc., consider the batch file a poison pill and delete it
    case HttpResultType::SentNoRequest:
    case HttpResultType::GotNoResponse_NonRetryable:
      return _process_and_upload_batch_result::bad_batch;

    // If we failed to complete the request due to transient network conditions, keep
    // the batch file around, but don't process any more batches for now
    case HttpResultType::GotNoResponse_Retryable:
      return _process_and_upload_batch_result::retryable_failure;

    // If we got a valid HTTP response, discriminate based on the status code
    case HttpResultType::GotResponse:
      // Any 200-level response indicates success
      if (res.status_code >= 200 && res.status_code <= 299) {
        return _process_and_upload_batch_result::success;
      }

      // Treat some client errors and most common server errors as retryable
      switch (res.status_code) {
        case 408:  // Request Timeout
        case 429:  // Too Many Requests
        case 500:  // Internal Server Error
        case 502:  // Bad Gateway
        case 503:  // Service Unavailable
        case 504:  // Gateway Timeout
        case 507:  // Insufficient Storage
          return _process_and_upload_batch_result::retryable_failure;

        // For all other status codes, break
        default:
          break;
      }

      // Treat all other responses as inherent flaws in the request payload itself
      return _process_and_upload_batch_result::bad_batch;
  }
  DATADOG_ASSERT(false, "unhandled HttpResultType enum value");
  return _process_and_upload_batch_result::bad_batch;
}

static _process_and_upload_batch_result _process_and_upload_batch(
    DiagnosticLogger& diagnostic_logger,
    Feature& feature_impl,
    HttpRequestBuilder& request_builder,
    FilesystemWrapper& fsw,
    const StoragePath& file_path,
    IHttpClient& http_client,
    std::vector<char>& mut_read_buffer
) {
  // This file is ready to process: attempt to open it for read
  const bool hold_advisory_lock = false;
  auto open_result = fsw.OpenForRead(file_path.CStr(), hold_advisory_lock);
  if (open_result.value != FilesystemResult::OK) {
    // If we failed to open the file for any reason, leave it in place and continue to
    // the next file
    return _process_and_upload_batch_result::retryable_failure;
  }

  // Initialize a BatchReader, which the feature implementation will use to iteratively
  // process each TLV block stored in the file
  BatchReader batch_reader(
      diagnostic_logger, std::move(open_result.file), mut_read_buffer
  );
  auto report = feature_impl.UploadThread_PrepareReport(batch_reader, request_builder);
  if (!report) {
    // If the feature elected not to generate a report from this batch, or was unable to
    // process it, delete the file and continue processing
    return _process_and_upload_batch_result::bad_batch;
  }

  // The report defines a POST endpoint and the HTTP headers that should be set on the
  // request, along with a body_writer function that our HTTP client will use to
  // populate the request body with chunked encoding: this allows us to stream data from
  // the batch file directly to the socket, without intermediate copies. As a result,
  // our open file handle MUST remain in scope until the HTTP request is finished.

  // Initiate an HTTP request, blocking until it finishes
  diagnostic_logger.Debug(
      "Initiating HTTP request", {{"method", "POST"}, {"url", report->url}}
  );
  const HttpResult res =
      http_client.Post(report->url, report->headers, report->body_writer);
  switch (res.type) {
    case HttpResultType::SentNoRequest:
      diagnostic_logger.Debug("Failed to send HTTP request", {{"url", report->url}});
      break;
    case HttpResultType::GotNoResponse_NonRetryable:
      diagnostic_logger.Debug(
          "Got no HTTP response", {{"url", report->url}, {"is_retryable", false}}
      );
      break;
    case HttpResultType::GotNoResponse_Retryable:
      diagnostic_logger.Debug(
          "Got no HTTP response", {{"url", report->url}, {"is_retryable", true}}
      );
      break;
    case HttpResultType::GotResponse:
      diagnostic_logger.Debug(
          "Got HTTP response",
          {{"url", report->url}, {"status_code", static_cast<int64_t>(res.status_code)}}
      );
      break;
  }

  // Interpret the result of our HTTP request and return the intended action, closing
  // the file in the process
  return _interpret_http_result(res);
}

static Duration _run_upload_cycle( // NOLINT(readability-function-cognitive-complexity)
    DiagnosticLogger& diagnostic_logger,
    UploadThreadConfig config,
    const platform::IClock& clock,
    RegisteredFeature& feature,
    IFilesystem& fs,
    IHttpClient& http_client,
    std::vector<std::string>& mut_filenames,
    std::vector<char>& mut_read_buffer
)
{
  // The feature should have its own state related to upload attempt timing etc.
  if (!feature.upload_state) {
    DATADOG_ASSERT(false, "registered feature has no upload state in upload thread");
    return ASSERTION_FAILURE_BACKOFF;
  }

  // The feature should also have a FeatureEventStorage object that provides access to
  // the directory where the Core stores batches of event data for this feature which we
  // have consent to upload
  if (!feature.storage) {
    DATADOG_ASSERT(false, "registered feature has no storage in upload thread");
    return ASSERTION_FAILURE_BACKOFF;
  }

  // Prepare a FilesystemWrapper that will handle path encoding transparently
  FilesystemWrapper fsw(fs);

  // Retrieve a list of all filenames in the consent-granted event directory
  mut_filenames.clear();
  const StoragePath& granted_dir_path = feature.storage->GetGrantedPath();
  const auto list_res = fsw.ListFiles(granted_dir_path.CStr(), mut_filenames);
  if (list_res != FilesystemResult::OK) {
    return feature.upload_state->current_delay;
  }

  // Sort all filenames lexicographically: this will cause an ordering bug for a brief
  // moment on November 20, 2286
  std::sort(mut_filenames.begin(), mut_filenames.end());

  // Iterate over the names of all files, starting with the oldest
  size_t num_uploads_attempted = 0;
  size_t num_uploads_completed_successfully = 0;
  _process_and_upload_batch_result last_batch_result{
      _process_and_upload_batch_result::success
  };
  for (const std::string& filename : mut_filenames) {
    // If a filename is all-integer, it's taken to be a Unix timestamp in milliseconds,
    // corresponding to the system_clock value that the storage thread read when it
    // created the file
    uint64_t timestamp_ms{0};
    const auto parse_result = std::from_chars(
        filename.data(), filename.data() + filename.size(), timestamp_ms
    );

    // Skip any files whose names are not strictly numeric
    const bool parse_ok = parse_result.ec == std::errc{};
    const bool parsed_full_string =
        parse_result.ptr == (filename.data() + filename.size());
    if (!parse_ok || !parsed_full_string) {
      continue;
    }

    // Read the system clock so we can determine the relative age of the file
    const Timestamp file_time{std::chrono::milliseconds(timestamp_ms)};
    const Timestamp now = clock.Now();

    // Defensive: guard against integer underflow on file age calculation. If the file
    // appears to be from the future, clamp its age to 0.
    Duration file_age = Duration::zero();
    if (file_time < now) {
      file_age = now - file_time;
    }

    // Under normal circumstances, we have to respect a minimum file age threshold
    // before we can read from files, as the storage thread may still be writing to them
    // before they hit that age. If our threshold is 0, it means we're permitted to
    // bypass these checks and read from all files, as long as they're not too _old_ to
    // upload.
    if (file_age < config.min_file_age_for_read) {
      // If we've encountered our first file that's too new to process, we're done. We
      // process files in order, sorted lexically by timestamp, so every file that we'd
      // encounter hereafter would be even newer than this one.
      break;
    }

    // Build the full path to the batch file we're examining
    StoragePath file_path;
    file_path.MustSet(granted_dir_path);
    if (!file_path.Append(filename)) {
      // Failure due to excessive path length is unexpected: the SDK writes these batch
      // files in the first place, so it would need to have constructed a valid path to
      // this file if it's a valid batch file.
      continue;
    }

    // If this file is too old to process, delete it and continue
    if (file_age >= config.max_file_age_for_read) {
      const auto delete_res = fsw.Delete(file_path.CStr());
      if (delete_res == FilesystemResult::OK) {
        diagnostic_logger.Debug(
            "Deleted outdated batch file",
            {{"feature", feature.name}, {"filename", filename}}
        );
      } else {
        // TODO: Keep track of the files we've processed, so that even if we fail to
        // delete a batch, we won't continually reupload it
        diagnostic_logger.Warning(
            "Failed to delete outdated batch file",
            {{"feature", feature.name},
             {"filename", filename},
             {"error", FilesystemResultStr(delete_res)}}
        );
      }
      continue;
    }

    // Read the batch of event data from the file, pass it to the feature implementation
    // to be processed, then initiate the resulting HTTP request to upload the batch to
    // intake
    last_batch_result = _process_and_upload_batch(
        diagnostic_logger,
        *feature.impl,
        feature.upload_state->request_builder,
        fsw,
        file_path,
        http_client,
        mut_read_buffer
    );
    num_uploads_attempted++;

    // Decide what to do with this file, and whether we should keep going
    bool should_delete_batch = false;
    bool should_abort_upload_cycle = false;
    switch (last_batch_result) {
      case _process_and_upload_batch_result::success:
        diagnostic_logger.Debug(
            "Batch upload OK; will delete and continue this upload cycle",
            {{"feature", feature.name}, {"filename", filename}}
        );
        num_uploads_completed_successfully++;
        should_delete_batch = true;
        break;
      case _process_and_upload_batch_result::retryable_failure:
        diagnostic_logger.Debug(
            "Batch upload failed; will abort this upload cycle and retry later",
            {{"feature", feature.name}, {"filename", filename}}
        );
        should_abort_upload_cycle = true;
        break;
      case _process_and_upload_batch_result::bad_batch:
        diagnostic_logger.Debug(
            "Batch rejected on upload; will delete and continue this upload cycle",
            {{"feature", feature.name}, {"filename", filename}}
        );
        should_delete_batch = true;
        break;
    }

    // If we need to delete the batch file, either because we processed it successfully
    // or because it's somehow malformed, delete it
    if (should_delete_batch) {
      const auto delete_res = fsw.Delete(file_path.CStr());
      if (delete_res == FilesystemResult::OK) {
        diagnostic_logger.Debug(
            "Deleted batch file", {{"feature", feature.name}, {"filename", filename}}
        );
      } else {
        // TODO: Keep track of the files we've processed, so that even if we fail to
        // delete a batch, we won't continually reupload it
        diagnostic_logger.Warning(
            "Failed to delete batch file",
            {{"feature", feature.name},
             {"filename", filename},
             {"error", FilesystemResultStr(delete_res)}}
        );
      }
    }

    // Break out of the loop if we don't want to continue processing batches
    if (should_abort_upload_cycle) {
      break;
    }

    // If we would otherwise keep going but we've hit our limit on the number of batches
    // processed in one upload cycle, stop processing
    if (num_uploads_attempted >= config.max_batches_per_cycle) {
      // Frequent occurrence of this log message may be an indication that the SDK is
      // accumulating data in storage faster than it can upload it
      diagnostic_logger.Debug(
          "Reached batch limit for this upload cycle",
          {{"feature", feature.name},
           {"num_uploads_attempted", num_uploads_attempted},
           {"max_batches_per_cycle", config.max_batches_per_cycle}}
      );
      break;
    }
  }

  if (num_uploads_attempted == 0) {
    diagnostic_logger.Debug(
        "Upload cycle finished with nothing to upload", {{"feature", feature.name}}
    );
  } else if (num_uploads_completed_successfully == num_uploads_attempted) {
    diagnostic_logger.Status(
        "Upload cycle finished with all uploads successful",
        {{"feature", feature.name}, {"num_uploads", num_uploads_attempted}}
    );
  } else {
    diagnostic_logger.Warning(
        "Upload cycle finished with unsuccessful uploads",
        {{"feature", feature.name},
         {"num_uploads_attempted", num_uploads_attempted},
         {"num_uploads_completed_successfully", num_uploads_completed_successfully}}
    );
  }

  // If we didn't find any batches to upload, leave our backoff interval unchanged
  if (num_uploads_attempted == 0) {
    // Schedule the next upload cycle after the same delay
    return feature.upload_state->current_delay;
  }

  // We did try to process one or more batches: modify this feature's backoff interval
  // based on the result of the last batch
  if (last_batch_result == _process_and_upload_batch_result::success) {
    // Last batch was good; schedule the next upload cycle to happen sooner
    return feature.upload_state->ResetDelayToMin();
  }

  // Last batch failed; wait a bit longer for the next upload cycle
  return feature.upload_state->IncreaseDelayTowardMax();
}

UploadThreadConfig::UploadThreadConfig(
    Duration in_min_file_age_for_read, size_t in_max_batches_per_cycle
)
    : min_file_age_for_read(in_min_file_age_for_read),
      max_batches_per_cycle(in_max_batches_per_cycle) {}

UploadThreadConfig UploadThreadConfig::FromCoreConfig(
    BatchSize batch_size, BatchProcessingLevel batch_processing_level
) {
  return UploadThreadConfig(
      BatchSize_ToMinFileAgeForRead(batch_size),
      BatchProcessingLevel_ToMaxBatchesPerCycle(batch_processing_level)
  );
}

UploadThreadState::UploadThreadState(
    const CoreContext& ctx, UploadFrequency upload_frequency
)
    : request_builder(ctx), current_delay{}, min_delay{}, max_delay{} {
  // Initialize best-case interval between requests, when network conditions are good
  // and uploads are succeeding
  switch (upload_frequency) {
    case UploadFrequency::Frequent:
      min_delay = from_seconds(3.0);
      break;
    case UploadFrequency::Average:
      min_delay = from_seconds(10.0);
      break;
    case UploadFrequency::Rare:
      min_delay = from_seconds(35.0);
      break;
  }

  // Clamp worst-case interval at 10x the best-case delay, and begin with an initial
  // starting point that's halfway between the two
  current_delay = min_delay * 5;
  max_delay = min_delay * 10;
}

Duration UploadThreadState::IncreaseDelayTowardMax() {
  const int64_t current = current_delay.count();
  const int64_t ten_percent = static_cast<int64_t>(static_cast<double>(current) * 0.1);
  current_delay = Duration(std::min<int64_t>(max_delay.count(), current + ten_percent));
  return current_delay;
}

Duration UploadThreadState::ResetDelayToMin() {
  current_delay = min_delay;
  return current_delay;
}

Duration Internal_HandleUploadProc(
    DiagnosticLogger& diagnostic_logger,
    UploadThreadConfig config,
    const platform::IClock& clock,
    FeatureId feature_id,
    std::vector<RegisteredFeature>& features,
    IFilesystem& fs,
    IHttpClient& http_client,
    std::vector<std::string>& mut_filenames,
    std::vector<char>& mut_read_buffer
) {
  // Find the feature that we want to initiate an upload cycle for
  const auto feature = std::find_if(
      features.begin(), features.end(), [feature_id](const RegisteredFeature& f) {
        return f.id == feature_id;
      }
  );

  // If we don't have a matching feature, something is wrong, since the set of
  // registered features can not be modified during the lifetime of the upload thread,
  // and the upload thread should be the only thing scheduling uploads
  if (feature == features.end()) {
    DATADOG_ASSERT(
        false, "feature_id on scheduled upload matches no registered feature"
    );
    return ASSERTION_FAILURE_BACKOFF;
  }

  // Kick off the next upload cycle for this feature, scanning the appropriate storage
  // directory for files that need to be uploaded, and uploading any that are found, up
  // to the configured limits
  return _run_upload_cycle(
      diagnostic_logger,
      config,
      clock,
      *feature,
      fs,
      http_client,
      mut_filenames,
      mut_read_buffer
  );
}

void UploadThreadMain(
    DiagnosticLogger& diagnostic_logger,
    UploadThreadConfig config,
    const platform::IClock& clock,
    UploadScheduler& scheduler,
    std::vector<RegisteredFeature>& features,
    IFilesystem& fs,
    IHttpClient& http_client
) {
  diagnostic_logger.Debug("Upload thread starting");

  // Schedule initial upload cycles for all features
  for (auto& feature : features) {
    scheduler.Schedule(feature.id, feature.upload_state->current_delay);
    diagnostic_logger.Debug(
        "Scheduled first upload cycle for feature",
        {{"feature", feature.name},
         {"feature_id", static_cast<int64_t>(feature.id)},
         {"delay_ms", feature.upload_state->current_delay.count() / 1000000}}
    );
  }

  // Initialize a reusable vector to contain filenames, so we don't have to allocate
  // every time we scan through a directory
  std::vector<std::string> filenames;
  filenames.reserve(64);

  // Similarly, initialize a reusable buffer to contain the current TLV block data read
  // from each file: we process uploads serially, so this buffer can be reused between
  // uploads and between features
  std::vector<char> read_buffer;
  read_buffer.reserve(QuantizeBufferSize(1024));

  // Run indefinitely, exiting once the scheduler returns nullopt
  while (auto feature_id = scheduler.WaitForNext()) {
    const Duration delay_until_next_cycle = Internal_HandleUploadProc(
        diagnostic_logger,
        config,
        clock,
        *feature_id,
        features,
        fs,
        http_client,
        filenames,
        read_buffer
    );
    scheduler.Schedule(*feature_id, delay_until_next_cycle);
    diagnostic_logger.Debug(
        "Scheduled next upload cycle for feature",
        {{"feature_id", static_cast<int64_t>(*feature_id)},
         {"delay_ms", delay_until_next_cycle.count() / 1000000}}
    );
  }

  diagnostic_logger.Debug("Upload thread finished");
}

}  // namespace datadog::impl
