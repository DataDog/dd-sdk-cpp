// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>

#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/types/crash_reporting.hpp"

namespace datadog::impl {

class File;

/**
 * Result of a call to ReadCrashContext. Use GetStatus() to differentiate between error
 * cases: on OK, we have valid context to include with our crash report; on ReadError,
 * we can signal `fs_error` and leave the file alone; and on Malformed, we can delete
 * the file.
 */
struct ReadCrashContextResult {
  std::optional<CrashContext> data;
  FilesystemResult fs_result{FilesystemResult::OK};

  enum class Status : uint8_t {
    OK,         // Valid file read with no errors; data has a value
    ReadError,  // A filesystem error occurred while reading; fs_error != OK
    Malformed   // File does not contain a valid, complete crash context
  };

  Status GetStatus() const {
    if (fs_result != FilesystemResult::OK) {
      return Status::ReadError;
    }
    if (!data.has_value()) {
      return Status::Malformed;
    }
    return Status::OK;
  }
};

/**
 * Given a handle to an open crash context file, reads the file and attempts to parse
 * its contents according to the format specified in crash_context.hpp.
 */
ReadCrashContextResult ReadCrashContext(File& file);

}  // namespace datadog::impl
