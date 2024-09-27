// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <memory>

#include "datadog/internal/utils.h"
#include "datadog/storage/datadog_file_system.h"
#include "datadog/time_provider.h"

namespace datadog::core {

using internal::no_default;

enum class TrackingConsent {
  Granted,
  NotGranted,
  Pending,
};

/// Defines the Datadog SDK policy when batching data together before uploading
/// it to Datadog servers. Smaller batches mean smaller but more network
/// requests, whereas larger batches mean fewer but larger network requests.
enum class BatchSize {
  /// Prefer small-sized data batches.
  Small,
  /// Prefer medium-sized data batches.
  Medium,
  /// Prefer large-sized data batches.
  Large,
};

/// Defines the frequency at which Datadog SDK will try to upload data batches.
enum class UploadFrequency {
  /// Try to upload batched data frequently.
  Frequent,
  /// Try to upload batched data with a medium frequency.
  Average,
  /// Try to upload batched data rarely.
  Rare,
};

/// Defines the maximum amount of batches processed sequentially without a delay
/// within one reading/uploading cycle.
enum class BatchProcessingLevel {
  Low,
  Medium,
  High,
};

struct DatadogConfiguration {
  no_default<TrackingConsent> tracking_consent;
  
  BatchSize batch_size{BatchSize::Medium};
  UploadFrequency upload_frequency{UploadFrequency::Average};
  BatchProcessingLevel batch_processing_level{BatchProcessingLevel::Medium};

  std::shared_ptr<storage::DatadogFileSystem> file_system{
      std::make_shared<storage::StdDatadogFileSystem>()};
};

}  // namespace datadog::core
