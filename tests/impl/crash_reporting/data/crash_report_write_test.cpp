// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_report_write.hpp"

#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/path.hpp"

#include "support/catch.hpp"
#include "support/crash_data.hpp"
#include "support/tempdir.hpp"

using namespace datadog::impl;

TEST_CASE("WriteCrashReport", "[unit][crash_reporting]") {
  // The routines defined in crash_report_write.hpp are async-signal-safe and use system
  // APIs (e.g. write(), WriteFile()) directly, without any indirection. Therefore we
  // can't inject a mock; we need to use a temp directory and actually write to disk.

  // Given a temporary directory where we can write test files
  TempDirectory tmpdir;

  // And the path to our crash report file, which we'll keep in that directory
  StoragePath path;
  REQUIRE(path.Set(tmpdir.path));
  REQUIRE(path.Append("crash"));
  PlatformPath platform_path;
  REQUIRE(platform_path.Encode(path.CStr()));

  // And an instance of the IFilesystem implementation for the current platform
  auto fs = CreateFilesystem();
  REQUIRE(fs);

  // And an open file handle for our crash report
  const bool append = false;
  const bool hold_advisory_lock = false;
  auto open_res = fs->OpenForWrite(platform_path, append, hold_advisory_lock);
  REQUIRE(open_res.value == FilesystemResult::OK);
  const PlatformFileHandle handle = open_res.handle;

  SECTION("M produce file with expected binary contents for latest format") {
    // Given the binary contents of the crash report file we expect to produce at the
    // current version
    const uint8_t* data_ptr = MOCK_CRASH_REPORT_V1;
    const size_t data_size = std::size(MOCK_CRASH_REPORT_V1);
    std::string_view data{reinterpret_cast<const char*>(data_ptr), data_size};

    // When we serialize a crash report using the same values that the mock data was
    // generated from
    {
      const uint64_t fault_code = 11;
      const uint64_t fault_address = 0;
      const uint64_t fault_flags = 0;
      const uint64_t pid = 100;
      const uint64_t tid = 101;
      const uint64_t timestamp = 1700000000000;
      WriteCrashReportHeader(
          handle, fault_code, fault_address, fault_flags, pid, tid, timestamp
      );
    }
    {
      const uint64_t start_address = 0x100000;
      const uint64_t end_address = 0x200000;
      const char* module_path = "/foo";
      const char* build_id = "abc";
      WriteCrashReportModule(handle, start_address, end_address, module_path, build_id);
    }
    {
      const uint64_t start_address = 0x300000;
      const uint64_t end_address = 0x400000;
      const char* module_path = "/bar";
      const char* build_id = "";
      WriteCrashReportModule(handle, start_address, end_address, module_path, build_id);
    }
    WriteCrashReportStackFrame(handle, 0x100100);
    WriteCrashReportStackFrame(handle, 0x100200);
    WriteCrashReportStackFrame(handle, 0x100300);
    WriteCrashReportStackFrame(handle, 0x100400);
    WriteCrashReportFooter(handle);

    // Then the file contains exactly the bytes we expect it to
    REQUIRE(tmpdir.FileExists("crash"));
    REQUIRE(tmpdir.ReadFileContents("crash") == data);
  }

  // Cleanup: close file handle
  fs->Close(handle);
}
