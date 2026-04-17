// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "datadog/impl/crash_reporting/data/crash_report.hpp"

namespace datadog::impl {

class File;

/**
 * In-memory representation of the complete contents of a crash report file.
 */
struct CrashReportFile {
  struct Module {
    uint64_t start_address;
    uint64_t end_address;
    std::string path;
    std::string build_id;
  };
  uint64_t fault_code;
  uint64_t fault_address;
  uint64_t fault_flags;
  uint64_t pid;
  uint64_t tid;
  uint64_t timestamp;
  std::vector<Module> modules;
  std::vector<uint64_t> stack_addresses;
};

/**
 * Result of a call to ReadCrashReport. data will be valid iff status is OK.
 *
 * If status is OK, we successfully read a complete, well-formed binary crash dump from
 * the file, and data contains all the information read from the file.
 *
 * If status is Empty, the file contained 0 bytes of data and should be silently
 * discarded, as it was likely opened by a process that never crashed. In this case,
 * data is std::nullopt.
 *
 * If status is Malformed, the file was truncated or improperly formatted and could not
 * be parsed. In this case, data is std::nullopt.
 */
struct ReadCrashReportResult {
  enum class Status : uint8_t { OK, Empty, Malformed };
  Status status;
  std::optional<CrashReportFile> data{std::nullopt};
};

/**
 * Given a handle to an open crash report file, reads the file and attempts to parse
 * its contents according to the format specified in crash_report.hpp.
 */
ReadCrashReportResult ReadCrashReport(File& file);

}  // namespace datadog::impl
