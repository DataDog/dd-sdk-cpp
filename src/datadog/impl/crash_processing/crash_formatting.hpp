// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>

namespace datadog::impl {

struct CrashReport;

/**
 * Given a crash report, returns a string suitable for use as `error.message` in the RUM
 * Error event describing that crash. Exact format is platform-dependent, but a typical
 * result looks something like:
 *
 * - "Application crash: EXCEPTION_ACCESS_VIOLATION (0xC0000005)"
 * - "Application crash: SIGSEGV (Segmentation fault)"
 */
std::string FormatCrashReportErrorMessage(const CrashReport& crash);

/**
 * Given a crash report, formats the stack trace, returning a multi-line string suitable
 * for use as `error.stack` in the RUM Error event describing the crash.
 *
 * Stack traces emitted on Windows, Linux, and macOS all share the same format, where
 * each frame consists of:
 *
 * - `<idx>  <module>   0x<instr_addr_16hex> 0x<load_addr_hex> + <offset_dec>`
 *
 * Each frame matches `^([0-9]+)\s+(.+)\s+(0x[0-9a-f]{16}) (0x[0-9a-f]+) \+ ([0-9]+)$`,
 * meaning that:
 *
 * - All hex adresses are formatted in lowercase with a leading '0x' prefix
 * - The instruction address is hex and is always padded to 16 hex digits
 * - The module load address is hex and need not be zero-padded
 * - The offset is decimal and follows the load address, joined with ' + '
 * - Any number of whitespace characters (1 or more) may be used after index and module
 * - Exactly one space is used between instruction address, load address, '+' delimiter,
 *   and offset
 *
 * Additionally:
 *
 * - Frame indices increase monotonically starting from 0 at the topmost frame
 * - If no resolved module information exists for a stack frame, its load address will
 *   be given as `0x0`, its offset will be `0`, and the module name will be `???`
 * - Each frame is delimited by '\n'
 * - No internal blank lines will be present between frames
 * - A final trailing '\n' may be present after the last frame (the backend tolerates it
 *   but does not require it)
 */
std::string FormatCrashReportStack(const CrashReport& crash);

}  // namespace datadog::impl
