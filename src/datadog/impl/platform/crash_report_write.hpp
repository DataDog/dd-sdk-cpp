// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "datadog/impl/platform/crash_report.hpp"

namespace datadog::platform {

#ifdef _WIN32
using CrashReportFileHandle = HANDLE;
#else
using CrashReportFileHandle = int;
#endif

/**
 * Given an open file descriptor for a crash report file, writes a file header that
 * describes the basic details of a crash.
 */
void WriteCrashReportHeader(
    CrashReportFileHandle fd,
    uint64_t fault_code,
    uint64_t fault_address,
    uint64_t fault_flags,
    uint64_t pid,
    uint64_t tid,
    uint64_t timestamp
);

/**
 * Given an open file descriptor for a crash report file, writes the details of a single
 * binary module that's loaded in the context of a crash.
 */
void WriteCrashReportModule(
    CrashReportFileHandle fd,
    uint64_t start_address,
    uint64_t end_address,
    const char* path
);

/**
 * Given an open file descriptor for a crash report file, writes the details of a single
 * address in the call stack for the crashing thread.
 */
void WriteCrashReportStackFrame(CrashReportFileHandle fd, uint64_t raw_address);

/**
 * Given an open file descriptor for a crash report file, writes a footer that indicates
 * the end of the crash report.
 */
void WriteCrashReportFooter(CrashReportFileHandle fd);

}  // namespace datadog::platform
