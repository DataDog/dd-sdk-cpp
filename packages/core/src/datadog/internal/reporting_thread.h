// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <memory>

#include "datadog/internal/core_internal.h"
#include "datadog/internal/performance_preset.h"
#include "datadog/storage/feature_storage.h"

namespace datadog::core::internal {

// Serves as an encapsulation of the threaded reporting / upload logic.
class ReportingThread {
 public:
  explicit ReportingThread(const std::weak_ptr<DatadogCoreInternal>& core,
                           PerformancePreset performance_preset)
      : is_started_{false},
        core_{core},
        performance_preset_{performance_preset} {}

  void Start();
  void Shutdown();

  // Internal - For testing single iterations in unit tests
  bool SingleReportingFrame();

 private:
  void ThreadProc();

  bool is_started_;

  std::weak_ptr<DatadogCoreInternal> core_;
  PerformancePreset performance_preset_;

  std::mutex shutdown_lock_;
  std::condition_variable shutdown_signal_;
  std::thread thread_;
};

}  // namespace datadog::core::internal
