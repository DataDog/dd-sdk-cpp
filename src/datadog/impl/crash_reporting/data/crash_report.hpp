// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>

/**
 * This file describes the binary format used for crash reports.
 *
 * Any crash handler implementation may write crash report files to the SDK's crashes/
 * directory. The CrashReporting feature will read these files on launch and attempt to
 * reconstitute and upload RUM Errors for those crashes.
 *
 * Crash report filenames are expected to have the following format:
 *
 * - crash_<system-timestamp-in-ms>_<pid>
 *
 * The system timestamp indicates the time at which the crash handler was initialized;
 * NOT the time at which a crash occurred. The presence of a crash report file does not
 * necessarily indicate that a crash has occurred: the file may remain open, entirely
 * empty, for the lifetime of the process.
 *
 * The writer (i.e. the ICrashHandler implementation) maintains an exclusive lock on the
 * file for the lifetime of the process. The reader (i.e. the CrashReporting feature
 * implementation) will attempt to acquire an exclusive lock before reading, processing,
 * and ultimately deleting the file.
 *
 * The contents of a crash report file are extremely simple in structure. All primitive
 * values are encoded as unsigned 64-bit integers in little-endian byte order. Strings
 * are encoded as a series of UTF-8 bytes, preceded by their length (number of bytes),
 * with no trailing terminator. Magic constants identify the layout of following values.
 *
 * This format is fixed: for any section identified by a magic constant, all values must
 * be present, in the order described in this file. Changes to the format should be
 * accomplished by incrementing CrashReportFileVersion, modifying the format as needed,
 * and maintaining backward-compatibility when parsing by checking the version number
 * encoded in the file.
 *
 * Example layout for a file describing a crash with two loaded modules ('/foo' with
 * build ID 'abc', and '/bar/' with no build ID) and a stack trace with 4 frames:
 *
 * 0x0000: <CrashReportHeaderMagic>
 * 0x0008: version
 * 0x0010: fault_code
 * 0x0018: fault_address
 * 0x0020: fault_flags
 * 0x0028: pid
 * 0x0030: tid
 * 0x0038: timestamp
 * 0x0040: <CrashReportModuleMagic>
 * 0x0048: start_address
 * 0x0050: end_address
 * 0x0058: num_path_bytes (4)
 * 0x0060: /
 * 0x0061: f
 * 0x0062: o
 * 0x0063: o
 * 0x0064: num_buildid_bytes (3)
 * 0x006c: a
 * 0x006d: b
 * 0x006e: c
 * 0x006f: <CrashReportModuleMagic>
 * 0x0077: start_address
 * 0x007f: end_address
 * 0x0087: num_path_bytes (4)
 * 0x008f: /
 * 0x0090: b
 * 0x0091: a
 * 0x0092: r
 * 0x0093: num_buildid_bytes (0)
 * 0x009b: <CrashReportStackFrameMagic>
 * 0x00a3: raw_address
 * 0x00ab: <CrashReportStackFrameMagic>
 * 0x00b3: raw_address
 * 0x00bb: <CrashReportStackFrameMagic>
 * 0x00c3: raw_address
 * 0x00cb: <CrashReportStackFrameMagic>
 * 0x00d3: raw_address
 * 0x00db: <CrashReportFooterMagic>
 */

namespace datadog::impl {

static const uint64_t CrashReportHeaderMagic = 0xdd01;
static const uint64_t CrashReportFileVersion = 1;

static const uint64_t CrashReportModuleMagic = 0xdda1;
static const uint64_t CrashReportStackFrameMagic = 0xdda2;

static const uint64_t CrashReportFooterMagic = 0xddff;

/**
 * Encodes basic details about a crash.
 */
struct CrashReportHeader {
  uint64_t version;        // Version number describing file format
  uint64_t fault_code;     // Signal number or exception code
  uint64_t fault_address;  // Address that triggered signal or exception
  uint64_t fault_flags;    // Exception flags on Windows; 0 otherwise
  uint64_t pid;            // PID of crashing process
  uint64_t tid;            // Thread ID of crashing thread
  uint64_t timestamp;      // Unix timestamp read directly from system clock
};

/**
 * Encodes basic details about a binary module that was loaded by the process at the
 * time of a crash.
 *
 * Binary format for each module entry:
 * - CrashReportModuleMagic (uint64_t)
 * - start_addr (uint64_t) - module base address
 * - end_addr (uint64_t) - first byte after module end
 * - path_length (uint64_t) - followed by path_length bytes of UTF-8 path data
 * - buildid_length (uint64_t) - followed by buildid_length bytes of UTF-8 build ID
 *
 * The path and build ID are encoded as length-prefixed strings, with no terminators.
 */
struct CrashReportModuleHeader {
  uint64_t start_addr;  // Address in virtual memory where this module is loaded
  uint64_t end_addr;    // First byte of virtual memory after the end of this module
};

}  // namespace datadog::impl
