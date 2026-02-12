// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/platform/crash_report_write.hpp"

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
  WriteFile(fd, bytes, num_bytes, &written, nullptr);
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
  WriteFile(fd, bytes, num_bytes, &written, nullptr);
  (void)written;
#else
  ssize_t result = write(fd, bytes, num_bytes);
  (void)result;
#endif
}

/**
 * Given a null-terminated string literal, writes that string value to the given file,
 * prefixing it with a uint64_t count of bytes. Does not write a null terminator to the
 * file.
 */
static void write_length_prefixed_string(CrashReportFileHandle fd, const char* str) {
  // Compute string length manually, as strlen is not guaranteed to be signal-safe
  uint64_t len = 0;
  while (str[len]) {
    len++;
  }

  // Write the number of (presumably UTF-8) bytes as a prefix, then write the string
  // thereafter, with no terminator
  write_uint64_values(fd, &len, 1);
  write_bytes(fd, str, len);
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
    const char* path
) {
  uint64_t values[] = {CrashReportModuleMagic, start_address, end_address};
  write_uint64_values(fd, static_cast<uint64_t*>(values), std::size(values));
  if (path) {
    write_length_prefixed_string(fd, path);
  } else {
    uint64_t zero = 0;
    write_uint64_values(fd, &zero, 1);
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
