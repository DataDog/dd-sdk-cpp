// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// CreateProfiler() for builds without profiling support. Compiled instead of
// dd_win_prof_profiler.cpp (see src/CMakeLists.txt).

#include <memory>

#include "datadog/impl/profiling/profiler.hpp"

namespace datadog::impl {

std::unique_ptr<IProfiler> CreateProfiler() { return nullptr; }

}  // namespace datadog::impl
