// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <dd-win-prof.h>
#include <internal/dd-win-rum-private.h>

#include <memory>

#include "datadog/impl/profiling/profiler_backend.hpp"

namespace datadog::impl {

/**
 * The dd-win-prof functions used by the backend. Tests replace these with fakes.
 */
struct DdWinProfApi {
  decltype(&SetupProfilingDataCallback) setup_callback;
  decltype(&SetupProfiler) setup;
  decltype(&StartProfiler) start;
  decltype(&StopProfiler) stop;
  decltype(&GetProfilingErrorMessage) error_message;
};

std::unique_ptr<IProfilerBackend> CreateDdWinProfBackendForTest(
    const DiagnosticLogger& logger, DdWinProfApi api
);

}  // namespace datadog::impl
