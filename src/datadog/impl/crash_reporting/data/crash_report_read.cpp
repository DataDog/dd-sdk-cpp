// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_report_read.hpp"

#include "datadog/impl/core/storage/filesystem_wrapper.hpp"

namespace datadog::impl {

static bool read_bytes(File& file, char* dst, size_t n) {
  auto res = file.Read(dst, n);
  return res.value == FilesystemResult::OK && res.bytes_read == n;
}

static bool read_uint64(File& file, uint64_t& out) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return read_bytes(file, reinterpret_cast<char*>(&out), sizeof(out));
}

static bool read_string(File& file, std::string& out, size_t max_len) {
  uint64_t length{};
  if (!read_uint64(file, length)) {
    return false;
  }
  if (length > max_len) {
    return false;
  }
  out.resize(length);
  return length == 0 || read_bytes(file, out.data(), length);
}

std::optional<CrashReportFile> ReadCrashReport(File& file) {
  // Parse header magic
  uint64_t magic{};
  if (!read_uint64(file, magic) || magic != CrashReportHeaderMagic) {
    return std::nullopt;
  }

  // Parse version magic: the only supported version is 1
  uint64_t version{};
  if (!read_uint64(file, version) || version != CrashReportFileVersion) {
    return std::nullopt;
  }

  // Default-construct a result value; use std::optional to ensure NRVO eligibility
  std::optional<CrashReportFile> result{std::in_place};

  // Populate crash metadata with values read from the file
  if (!read_uint64(file, result->fault_code)) {
    return std::nullopt;
  }
  if (!read_uint64(file, result->fault_address)) {
    return std::nullopt;
  }
  if (!read_uint64(file, result->fault_flags)) {
    return std::nullopt;
  }
  if (!read_uint64(file, result->pid)) {
    return std::nullopt;
  }
  if (!read_uint64(file, result->tid)) {
    return std::nullopt;
  }
  if (!read_uint64(file, result->timestamp)) {
    return std::nullopt;
  }

  // Dispatch on magic to parse variable-length module and stack frame entries, until we
  // encounter the footer or an unrecognized token
  size_t num_modules_parsed = 0;
  size_t num_stack_frames_parsed = 0;
  for (;;) {
    // Read the next 8-byte value from the file, aborting if we've hit EOF without
    // seeing the footer magic
    uint64_t token{};
    if (!read_uint64(file, token)) {
      return std::nullopt;
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
        return std::nullopt;
      }

      // Module magic introduces details of a loaded module: add a new entry into our
      // result struct's modules vector, then populate that value with module details
      // read from the file
      auto& mod = result->modules.emplace_back();
      if (!read_uint64(file, mod.start_address)) {
        return std::nullopt;
      }
      if (!read_uint64(file, mod.end_address)) {
        return std::nullopt;
      }
      if (!read_string(file, mod.path, 4096)) {
        return std::nullopt;
      }
      if (!read_string(file, mod.build_id, 256)) {
        return std::nullopt;
      }
    } else if (token == CrashReportStackFrameMagic) {
      // Set an upper bound on stack frames
      if (++num_stack_frames_parsed > 512) {
        return std::nullopt;
      }

      // Stack frame magic introduces a raw stack frame address; read it and push it
      // onto our result's struct's stack address vector
      uint64_t address{};
      if (!read_uint64(file, address)) {
        return std::nullopt;
      }
      result->stack_addresses.push_back(address);
    } else {
      // Unrecognized magic: file is malformed or incomplete
      return std::nullopt;
    }
  }

  // Return our result value with all accumulated data
  return result;
}

}  // namespace datadog::impl
