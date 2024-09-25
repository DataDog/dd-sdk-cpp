// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>

#include "datadog/internal/performance_preset.h"
#include "datadog/storage/datadog_file_system.h"
#include "datadog/time_provider.h"

namespace datadog::core::storage {

// FeatureStorage operates as the method for writing and reading batches for a
// particular feature, as well as handling moving batches when tracking consent
// changes.  Each feature should have its own storage, created when the feature
// is created.
class FeatureStorage {
 public:
  explicit FeatureStorage(const std::string& feature_name,
                          const internal::PerformancePreset& performance_preset,
                          datadog::core::DateTimeProvider date_time_provider,
                          std::shared_ptr<IDatadogFileSystem> file_system);

  bool Write(std::string_view data);

 private:
  bool CanReuseCurrentFile(size_t write_size);
  bool CreateNewWritableFile();

  std::string feature_name_;
  internal::PerformancePreset performance_preset_;
  DateTimeProvider date_time_provider_;
  std::shared_ptr<IDatadogFileSystem> file_system_;
  std::mt19937 random_generator_;

  std::unique_ptr<DatadogFile> current_file_;
  // These times are in nanoseconds since epoch, which is
  // what is returned from DateTimeProvider
  uint64_t current_file_creation_time_;
  uintmax_t current_file_size_;
};

}  // namespace datadog::core::storage
