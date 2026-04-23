// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>

#include "datadog/impl/core/feature_types/crash_reporting.hpp"

namespace datadog::impl {

struct CrashReportFile;
struct CrashContextFile;

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

}  // namespace datadog::impl
