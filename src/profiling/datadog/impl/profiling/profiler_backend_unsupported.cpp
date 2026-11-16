// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// CreateProfilerBackend() for builds without profiling support. Compiled instead of
// dd_win_prof_profiler_backend.cpp (see src/CMakeLists.txt).

#include <memory>

#include "datadog/impl/profiling/profiler_backend.hpp"

namespace datadog::impl {

std::unique_ptr<IProfilerBackend> CreateProfilerBackend(
    const DiagnosticLogger& logger
) {
  logger.Status(
      "Profiling is not supported in this build of the SDK: it requires Windows x64 "
      "and the DD_ENABLE_PROFILING CMake option"
  );
  return nullptr;
}

}  // namespace datadog::impl
