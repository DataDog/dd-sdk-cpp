// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "datadog/impl/core/storage/filesystem.hpp"
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
 * Result of a call to ReadCrashReport. Use GetStatus() to differentiate between error
 * cases: on OK, we have a crash to process; on Empty, we can safely ignore the file;
 * on ReadError, we can signal `fs_error` and leave the file alone; and on Malformed, we
 * can delete the file.
 */
struct ReadCrashReportResult {
  std::optional<CrashReportFile> data{std::nullopt};
  FilesystemResult fs_result{FilesystemResult::OK};
  bool empty{false};

  enum class Status : uint8_t {
    OK,         // Valid file read with no errors; data has a value
    Empty,      // File was read OK but contained 0 bytes; does not represent a crash
    ReadError,  // A filesystem error occurred while reading; fs_result != OK
    Malformed   // File does not contain a valid, complete crash report
  };

  Status GetStatus() const {
    if (fs_result != FilesystemResult::OK) {
      return Status::ReadError;
    }
    if (empty) {
      return Status::Empty;
    }
    if (!data.has_value()) {
      return Status::Malformed;
    }
    return Status::OK;
  }
};

/**
 * Given a handle to an open crash report file, reads the file and attempts to parse
 * its contents according to the format specified in crash_report.hpp.
 */
ReadCrashReportResult ReadCrashReport(File& file);

}  // namespace datadog::impl
