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
 * Given a handle to an open crash report file, reads the file and attempts to parse
 * its contents according to the format specified in crash_report.hpp.
 */
std::optional<CrashReportFile> ReadCrashReport(File& file);

}  // namespace datadog::impl
