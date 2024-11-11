// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/internal/reporting_thread.h"

#include <thread>

namespace datadog::core::internal {

using datadog::core::reporting::DatadogReporter;

void ReportingThread::Start() {
  if (is_started_) {
    // TELEM: Start this thread twice is bad
    return;
  }

  is_started_ = true;
  thread_ = std::thread(&ReportingThread::ThreadProc, this);
}

void ReportingThread::Shutdown() {
  if (!is_started_) {
    // TELEM: Thread wasn't started
    return;
  }

  {
    std::lock_guard<std::mutex> lock(shutdown_lock_);
    is_started_ = false;
  }
  shutdown_signal_.notify_all();

  thread_.join();
}

void ReportingThread::ThreadProc() {
  while (is_started_) {
    if (!SingleReportingFrame()) {
      // Failure on a reporting frame is fatal.
      return;
    }

    {
      // Wait for a set amount of time, but let shutdown interrupt
      std::unique_lock<std::mutex> lock(shutdown_lock_);
      // TOOD: Vary upload delay based on success / failure.
      shutdown_signal_.wait_for(lock, performance_preset_.min_upload_delay());
    }
  }
}

bool ReportingThread::SingleReportingFrame() {
  if (auto core = core_.lock()) {
    static const int kReservedFilePaths = 10;

    // This may seem like an odd choice, but by keeping this vector outside of
    // the loop and clearing it instead of creating a new one, we keep the
    // number of allocations to a minimum.
    std::vector<std::filesystem::path> readable_files;
    readable_files.reserve(kReservedFilePaths);

    const auto& features_by_id = core->GetFeatureInfoMap();
    auto reporter = core->GetReporter();

    for (const auto& [id, feature_info] : features_by_id) {
      const auto& storage = feature_info.storage;

      if (storage->ListReadableFiles(readable_files)) {
        uint32_t files_to_process =
            std::min(performance_preset_.max_batches_per_upload(),
                     static_cast<uint32_t>(readable_files.size()));
        for (uint32_t i = 0; i < files_to_process; ++i) {
          const auto& path = readable_files[i];
          auto file = storage->GetReadableFile(path);
          auto report = feature_info.feature->CreateReportFromBatch(*file);
          switch (reporter->Send(report)) {
            case DatadogReporter::Status::Ok:
              storage->DeleteReadableFile(std::move(file));
              break;
            case DatadogReporter::Status::ErrorNeedsRetry:
              // Immediately break out of sending files
              return true;
            case DatadogReporter::Status::UnrecoverableError:
              // Delete this file, but allow processing to continue
              storage->DeleteReadableFile(std::move(file));
              break;
          }
        }
      }

      readable_files.clear();
    }
    return true;
  } else {
    // TELEM: We've lost the Core -- this shouldn't happen
    return false;
  }
}

}  // namespace datadog::core::internal
