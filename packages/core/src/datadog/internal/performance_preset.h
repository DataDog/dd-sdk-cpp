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
  using Nanoseconds = std::chrono::duration<uint64_t, std::nano>;

  explicit PerformancePreset(BatchSize batch_size,
                             UploadFrequency upload_frequency,
                             BatchProcessingLevel batch_processing_level);

  // For testing or override purposes only
  explicit PerformancePreset(uint64_t max_file_size,
                             uint64_t max_directory_size,
                             Nanoseconds max_file_age_for_write,
                             Nanoseconds min_file_age_for_read,
                             Nanoseconds initial_upload_delay,
                             Nanoseconds min_upload_delay,
                             Nanoseconds max_upload_delay,
                             uint32_t max_batches_per_upload)
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

  Nanoseconds max_file_age_for_write() const { return max_file_age_for_write_; }
  Nanoseconds min_file_age_for_read() const { return min_file_age_for_read_; }
  constexpr Nanoseconds max_file_age_for_read() const {
    return std::chrono::hours(18);
  }

  Nanoseconds initial_upload_delay() const { return initial_upload_delay_; }
  Nanoseconds min_upload_delay() const { return min_upload_delay_; }
  Nanoseconds max_upload_delay() const { return max_upload_delay_; }

  uint32_t max_batches_per_upload() const { return max_batches_per_upload_; }

 private:
  uint64_t max_file_size_;
  uint64_t max_directory_size_;
  Nanoseconds max_file_age_for_write_;
  Nanoseconds min_file_age_for_read_;

  Nanoseconds initial_upload_delay_;
  Nanoseconds min_upload_delay_;
  Nanoseconds max_upload_delay_;

  uint32_t max_batches_per_upload_;
};

}  // namespace datadog::core::internal
