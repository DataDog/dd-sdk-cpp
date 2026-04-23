// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <functional>
#include <optional>
#include <string>

#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/feature_types/crash_reporting.hpp"

namespace datadog::impl {

class DiagnosticLogger;
class IFilesystem;
class FilesystemWrapper;
class StoragePath;
struct CrashReportFile;
struct CrashContextFile;

/**
 * Function used to signal that a valid crash report has been detected and processed.
 */
using CrashReportCallback = std::function<void(CrashReport)>;

/**
 * Helper function used to build a `CrashReport` struct based on the information read
 * from a crash_<timestamp>_<pid> and crash_<timestamp>_<pid>.ctx file.
 *
 * In the process of condensing this file-format-specific data into a single struct, we
 * resolve the binary module and address offset associated with each stack frame, and
 * filter the list of loaded modules such that it only includes modules that are
 * directly referenced in the call stack.
 */
CrashReport BuildCrashReport(
    const CrashReportFile& crf, const std::optional<CrashContextFile>& ccf
);

/**
 * Helper function used to detect whether a file is likely to be a valid binary crash
 * report.
 */
bool IsCrashReportFilename(const std::string& filename);

/**
 * Scans for crash report files left behind by previous application processes, producing
 * CrashReport values for each set of files that represents a valid crash.
 *
 * This routine is typically invoked by the CrashReporting feature on SDK start, but
 * it's offloaded to the context thread so as not to block the main thread.
 *
 * @param logger Logger used to emit diagnostic warnings in case of failure, status
 *  messages on success.
 * @param fs IFilesystem interface used to list, read, and delete files on disk.
 * @param storage_dir_path Path to the directory where crash-related artifacts for
 * this application are conventionally stored.
 * @param on_process_callback Callback used to generate CrashReport structs built
 * from valid crash report files.
 */
void ProcessCrashReports(
    DiagnosticLogger& logger,
    IFilesystem& fs,
    StoragePath& storage_dir_path,
    const CrashReportCallback& on_process_callback
);

}  // namespace datadog::impl
