// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <chrono>

#include "datadog/core_configuration.h"

namespace datadog::core::internal {

using datadog::core::BatchProcessingLevel;
using datadog::core::BatchSize;
using datadog::core::UploadFrequency;

// Translation of the configuration enum into properties used by the Core to
// detemine when to start new batches, when to upload batches, and when to clear
// out old data.
class PerformancePreset {
 public:
  using DurationNs = std::chrono::duration<uint64_t, std::nano>;

  explicit PerformancePreset(BatchSize batch_size,
                             UploadFrequency upload_frequency,
                             BatchProcessingLevel batch_processing_level);

  // For testing or override purposes only
  explicit PerformancePreset(uint64_t max_file_size,
                             uint64_t max_directory_size,
                             DurationNs max_file_age_for_write,
                             DurationNs min_file_age_for_read,
                             DurationNs initial_upload_delay,
                             DurationNs min_upload_delay,
                             DurationNs max_upload_delay,
                             int32_t max_batches_per_upload)
      : max_file_size_(max_file_size),
        max_directory_size_(max_directory_size),
        max_file_age_for_write_(max_file_age_for_write),
        min_file_age_for_read_(min_file_age_for_read),
        initial_upload_delay_(initial_upload_delay),
        min_upload_delay_(min_upload_delay),
        max_upload_delay_(max_upload_delay),
        max_batches_per_upload_(max_batches_per_upload) {}

  uint64_t max_file_size() const { return max_file_size_; }
  uint64_t max_directory_size() const { return max_directory_size_; }

  DurationNs max_file_age_for_write() const { return max_file_age_for_write_; }
  DurationNs min_file_age_for_read() const { return min_file_age_for_read_; }
  constexpr DurationNs max_file_age_for_read() const {
    return std::chrono::duration_cast<DurationNs>(std::chrono::hours(18));
  }

  DurationNs initial_upload_delay() const { return initial_upload_delay_; }
  DurationNs min_upload_delay() const { return min_upload_delay_; }
  DurationNs max_upload_delay() const { return max_upload_delay_; }

  int32_t max_batches_per_upload() const { return max_batches_per_upload_; }

 private:
  constexpr static uint64_t mbToBytes(int64_t mb) { return mb * 1024 * 1024; }

  uint64_t max_file_size_;
  uint64_t max_directory_size_;

  DurationNs max_file_age_for_write_;
  DurationNs min_file_age_for_read_;

  DurationNs initial_upload_delay_;
  DurationNs min_upload_delay_;
  DurationNs max_upload_delay_;

  int32_t max_batches_per_upload_;
};

}  // namespace datadog::core::internal
