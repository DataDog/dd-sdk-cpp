// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/crash_reporting/crash_handler.hpp"

namespace datadog::impl {

namespace CrashHandler {

/**
 * On first call, creates and initializes the process-global ICrashHandler instance to
 * be used by the CrashReporting feature. If ICrashHandler::Initialize() succeeds on the
 * first call to this function, returns a pointer to the handler value, which remains
 * alive for the duration of the process.
 *
 * If initialization fails on the first call, returns null. On all subsequent calls
 * after the first, regardless of whether initialization succeeded, returns null.
 *
 * @param logger Allows diagnostic messages to be emitted during initialization. Passed
 *  by value to ensure that the handler can not retain references to state owned by the
 *  Core. DiagnosticLogger wraps a std::function that is safe to call throughout the
 *  lifetime of the process: the handler MAY store the logger value and use it later.
 *  However, the handler MUST NOT use the logger during a crash, as it can not be
 *  assumed that the diagnostic-handler callback is async-signal-safe.
 * @param crash_storage_dir_path Path to the directory where this handler may store
 *  crash-related artifacts.
 * @param helper_exe_path Configured path to a helper executable that may be used by the
 *  crash handler, if any. Valid only for the scope of the function call.
 */
ICrashHandler* InitializeOnce(
    DiagnosticLogger logger,
    std::string_view crash_storage_dir_path,
    std::string_view helper_exe_path
);

}  // namespace CrashHandler

}  // namespace datadog::impl
