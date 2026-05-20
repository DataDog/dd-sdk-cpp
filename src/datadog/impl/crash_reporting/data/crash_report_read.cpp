// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_report_read.hpp"

#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/crash_reporting/data/crash_read_util.hpp"

namespace datadog::impl {

// Suppress warnings re: cyclomatic complexity; branches are all straightforward
// early-outs on parse failure
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
ReadCrashReportResult ReadCrashReport(File& file) {
  // Parse header magic, without using our helper function(s) so we can distinguish an
  // empty file (0 bytes on first read) from a truncated or malformed one
  uint64_t magic{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto magic_res = file.Read(reinterpret_cast<char*>(&magic), sizeof(magic));
  if (magic_res.value == FilesystemResult::OK && magic_res.bytes_read == 0) {
    return {std::nullopt, magic_res.value, true};
  }
  if (magic_res.value != FilesystemResult::OK ||
      magic_res.bytes_read != sizeof(magic) || magic != CrashReportHeaderMagic) {
    return {std::nullopt, magic_res.value, false};
  }

  // Parse version magic: the only supported version is 1
  uint64_t version{};
  if (auto res = ReadUInt64(file, version); !res.OK()) {
    return {std::nullopt, res.value, false};
  }
  if (version != CrashReportFileVersion) {
    return {std::nullopt, FilesystemResult::OK, false};
  }

  // Default-construct a struct that we'll populate as we read from the file and return
  // only upon successful completion
  CrashReportFile data;

  // Populate crash metadata with values read from the file
  if (auto res = ReadUInt64(file, data.fault_code); !res.OK()) {
    return {std::nullopt, res.value, false};
  }
  if (auto res = ReadUInt64(file, data.fault_address); !res.OK()) {
    return {std::nullopt, res.value, false};
  }
  if (auto res = ReadUInt64(file, data.fault_flags); !res.OK()) {
    return {std::nullopt, res.value, false};
  }
  if (auto res = ReadUInt64(file, data.pid); !res.OK()) {
    return {std::nullopt, res.value, false};
  }
  if (auto res = ReadUInt64(file, data.tid); !res.OK()) {
    return {std::nullopt, res.value, false};
  }
  if (auto res = ReadUInt64(file, data.timestamp_ms); !res.OK()) {
    return {std::nullopt, res.value, false};
  }

  // Dispatch on magic to parse variable-length module and stack frame entries, until we
  // encounter the footer or an unrecognized token
  size_t num_modules_parsed = 0;
  size_t num_stack_frames_parsed = 0;
  for (;;) {
    // Read the next 8-byte value from the file, aborting if we've hit EOF without
    // seeing the footer magic
    uint64_t token{};
    if (auto res = ReadUInt64(file, token); !res.OK()) {
      return {std::nullopt, res.value, false};
    }

    // Footer magic indicates clean end of file; break and return our value
    if (token == CrashReportFooterMagic) {
      break;
    }

    // If it's any other magic value, branch to the appropriate routine to handle that
    // data. If it's not a recognized magic value, abort.
    if (token == CrashReportModuleMagic) {
      // Set a sane upper bound on the number of module entries that a valid crash
      // report file can contain, to prevent runaway parsing or unbounded allocation
      if (++num_modules_parsed > 4096) {
        return {std::nullopt, FilesystemResult::OK, false};
      }

      // Module magic introduces details of a loaded module: add a new entry into our
      // result struct's modules vector, then populate that value with module details
      // read from the file
      auto& mod = data.modules.emplace_back();
      if (auto res = ReadUInt64(file, mod.start_address); !res.OK()) {
        return {std::nullopt, res.value, false};
      }
      if (auto res = ReadUInt64(file, mod.end_address); !res.OK()) {
        return {std::nullopt, res.value, false};
      }
      if (auto res = ReadString(file, mod.path, 4096); !res.OK()) {
        return {std::nullopt, res.value, false};
      }
      if (auto res = ReadString(file, mod.build_id, 256); !res.OK()) {
        return {std::nullopt, res.value, false};
      }
    } else if (token == CrashReportStackFrameMagic) {
      // Set an upper bound on stack frames
      if (++num_stack_frames_parsed > 512) {
        return {std::nullopt, FilesystemResult::OK, false};
      }

      // Stack frame magic introduces a raw stack frame address; read it and push it
      // onto our result's struct's stack address vector
      uint64_t address{};
      if (auto res = ReadUInt64(file, address); !res.OK()) {
        return {std::nullopt, res.value, false};
      }
      data.stack_addresses.push_back(address);
    } else {
      // Unrecognized magic: file is malformed or incomplete
      return {std::nullopt, FilesystemResult::OK, false};
    }
  }

  // Return our result value with all accumulated data
  return ReadCrashReportResult{std::move(data), FilesystemResult::OK, false};
}

}  // namespace datadog::impl
