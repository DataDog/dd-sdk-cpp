// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/platform/crash_report_write.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

#include <array>
#include <cstddef>

namespace datadog::platform {

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)

static void write_bytes(CrashReportFileHandle fd, const char* bytes, size_t num_bytes) {
#ifdef _WIN32
  DWORD written = 0;
  WriteFile(fd, bytes, static_cast<DWORD>(num_bytes), &written, nullptr);
  (void)written;
#else
  ssize_t result = write(fd, bytes, num_bytes);
  (void)result;
#endif
}

static void write_uint64_values(
    CrashReportFileHandle fd, const uint64_t* values, size_t num_values
) {
  const char* bytes = reinterpret_cast<const char*>(values);
  const size_t num_bytes = num_values * sizeof(uint64_t);

#ifdef _WIN32
  DWORD written = 0;
  WriteFile(fd, bytes, static_cast<DWORD>(num_bytes), &written, nullptr);
  (void)written;
#else
  ssize_t result = write(fd, bytes, num_bytes);
  (void)result;
#endif
}

void WriteCrashReportHeader(
    CrashReportFileHandle fd,
    uint64_t fault_code,
    uint64_t fault_address,
    uint64_t fault_flags,
    uint64_t pid,
    uint64_t tid,
    uint64_t timestamp
) {
  uint64_t values[] = {
      CrashReportHeaderMagic,
      CrashReportFileVersion,
      fault_code,
      fault_address,
      fault_flags,
      pid,
      tid,
      timestamp
  };
  write_uint64_values(fd, static_cast<uint64_t*>(values), std::size(values));
}

void WriteCrashReportModule(
    CrashReportFileHandle fd,
    uint64_t start_address,
    uint64_t end_address,
    const char* path,
    const char* build_id
) {
  // Compute string lengths manually for async-signal-safety
  uint64_t path_len = 0;
  if (path) {
    while (path[path_len]) {
      path_len++;
    }
  }

  uint64_t build_id_len = 0;
  if (build_id) {
    while (build_id[build_id_len]) {
      build_id_len++;
    }
  }

  // Write module header: magic, start, end
  uint64_t header_values[] = {CrashReportModuleMagic, start_address, end_address};
  write_uint64_values(
      fd, static_cast<uint64_t*>(header_values), std::size(header_values)
  );

  // Write path as length-prefixed string
  write_uint64_values(fd, &path_len, 1);
  if (path_len > 0) {
    write_bytes(fd, path, path_len);
  }

  // Write build ID as length-prefixed string
  write_uint64_values(fd, &build_id_len, 1);
  if (build_id_len > 0) {
    write_bytes(fd, build_id, build_id_len);
  }
}

void WriteCrashReportStackFrame(CrashReportFileHandle fd, uint64_t raw_address) {
  uint64_t values[] = {CrashReportStackFrameMagic, raw_address};
  write_uint64_values(fd, static_cast<uint64_t*>(values), std::size(values));
}

void WriteCrashReportFooter(CrashReportFileHandle fd) {
  write_uint64_values(fd, &CrashReportFooterMagic, 1);
}

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

}  // namespace datadog::platform
