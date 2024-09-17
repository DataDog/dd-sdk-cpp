// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/internal/performance_preset.h"

#include "datadog/core_configuration.h"

namespace datadog::core::internal {

// This file is full of magic numbers according to clang-tidy,
// quiet it down a bit
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

// All members should be initialized in the constructor code block
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
PerformancePreset::PerformancePreset(
    BatchSize batch_size,
    UploadFrequency upload_frequency,
    BatchProcessingLevel batch_processing_level) {
  using std::chrono::duration_cast;
  using std::chrono::milliseconds;
  using std::chrono::seconds;

  std::chrono::seconds mean_file_age;
  switch (batch_size) {
    case BatchSize::Small:
      mean_file_age = seconds(3);
      break;

    case BatchSize::Medium:
      mean_file_age = seconds(10);
      break;

    case BatchSize::Large:
      mean_file_age = seconds(35);
      break;
  }
  max_file_age_for_write_ = duration_cast<DurationNs>(mean_file_age * 0.95f);
  min_file_age_for_read_ = duration_cast<DurationNs>(mean_file_age * 1.05f);

  switch (upload_frequency) {
    case UploadFrequency::Frequent:
      min_upload_delay_ = duration_cast<DurationNs>(milliseconds(500));
      break;

    case UploadFrequency::Average:
      min_upload_delay_ = duration_cast<DurationNs>(seconds(2));
      break;

    case UploadFrequency::Rare:
      min_upload_delay_ = duration_cast<DurationNs>(seconds(5));
      break;
  }

  initial_upload_delay_ = min_upload_delay_ * 5;
  max_upload_delay_ = min_upload_delay_ * 10;

  switch (batch_processing_level) {
    case BatchProcessingLevel::Low:
      max_batches_per_upload_ = 1;
      break;

    case BatchProcessingLevel::Medium:
      max_batches_per_upload_ = 10;
      break;
    case BatchProcessingLevel::High:
      max_batches_per_upload_ = 100;
      break;
  }
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

}  // namespace datadog::core::internal
