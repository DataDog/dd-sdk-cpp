// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

// dd-win-prof.h includes <windows.h>, so windows_headers.hpp must come first: it
// defines NOMINMAX, which keeps std::max working in later SDK headers.
// clang-format off
#include "datadog/impl/types/windows_headers.hpp"
// clang-format on

#include <dd-win-prof.h>
#include <internal/dd-win-rum-private.h>

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

/**
 * Creates a dd-win-prof backend that calls `api` instead of dd-win-prof. Like
 * CreateProfilerBackend(), it claims the process-wide backend slot until the backend is
 * destroyed, so it returns null, after logging the reason, while another backend
 * exists.
 */
std::unique_ptr<IProfilerBackend> CreateDdWinProfBackendForTest(
    const DiagnosticLogger& logger, DdWinProfApi api
);

}  // namespace datadog::impl
