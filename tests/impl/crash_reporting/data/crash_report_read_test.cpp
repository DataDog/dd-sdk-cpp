// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_report_read.hpp"

#include "datadog/impl/core/storage/filesystem_wrapper.hpp"

#include "mock/filesystem.hpp"
#include "support/catch.hpp"
#include "support/crash_data.hpp"

using namespace datadog::impl;

TEST_CASE("ReadCrashReport", "[unit][crash_reporting]") {
  // Given a mock filesystem
  MockFilesystem fs;

  // And the binary contents of an example crash report file
  const std::string_view data{
      reinterpret_cast<const char*>(MOCK_CRASH_REPORT_V1),
      std::size(MOCK_CRASH_REPORT_V1)
  };

  SECTION("M return value with expected field values W file is a valid crash report") {
    // Given a file that contains our golden crash report data
    fs.Touch("crash", data);

    // And an open file handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get a valid result value
    REQUIRE(result.has_value());

    // And the header fields contain the values encoded in the mock binary data
    REQUIRE(result->fault_code == 11);  // SIGSEGV
    REQUIRE(result->fault_address == 0);
    REQUIRE(result->fault_flags == 0);
    REQUIRE(result->pid == 100);
    REQUIRE(result->tid == 101);
    REQUIRE(result->timestamp == 1700000000000ULL);

    // And there are two modules with the expected fields
    REQUIRE(result->modules.size() == 2);
    REQUIRE(result->modules[0].start_address == 0x100000);
    REQUIRE(result->modules[0].end_address == 0x200000);
    REQUIRE(result->modules[0].path == "/foo");
    REQUIRE(result->modules[0].build_id == "abc");
    REQUIRE(result->modules[1].start_address == 0x300000);
    REQUIRE(result->modules[1].end_address == 0x400000);
    REQUIRE(result->modules[1].path == "/bar");
    REQUIRE(result->modules[1].build_id == "");

    // And there are four stack frame addresses
    REQUIRE(result->stack_addresses.size() == 4);
    REQUIRE(result->stack_addresses[0] == 0x100100);
    REQUIRE(result->stack_addresses[1] == 0x100200);
    REQUIRE(result->stack_addresses[2] == 0x100300);
    REQUIRE(result->stack_addresses[3] == 0x100400);
  }

  SECTION("M return no value W file has invalid header magic") {
    // Given a file that contains garbage data
    fs.Touch("crash", "hello-world-this-file-is-not-a-valid-crash-report");

    // And an open file handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get no value
    REQUIRE(!result.has_value());
  }

  SECTION("M return no value W file has invalid footer magic") {
    // Given a file that contains valid crash report data, with the exception of a
    // missing footer
    const std::string_view data_less_8_bytes{data.data(), data.size() - 8};
    fs.Touch("crash", data_less_8_bytes);

    // And an open file handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get no value
    REQUIRE(!result.has_value());
  }

  SECTION("M return no value W file contains module path with len > 4096") {
    // Given a file that contains valid crash report data, but with one of the module
    // entries containing an excessively long path
    const auto path_pos = data.find("/foo");
    REQUIRE(path_pos != std::string_view::npos);
    const std::string_view head{data.data(), path_pos - 8};
    const std::string_view tail{
        data.data() + path_pos + 4, data.size() - (path_pos + 4)
    };
    const uint64_t long_path_len = 4100;
    const std::string len_bytes(
        reinterpret_cast<const char*>(&long_path_len), sizeof(long_path_len)
    );
    const std::string long_path(4100, 'A');
    const std::string new_data =
        std::string(head) + len_bytes + long_path + std::string(tail);
    fs.Touch("crash", new_data);

    // And an open file handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get no value: the function aborts rather than potentially making a huge
    // heap allocation that might be the result of a malformed or misinterpreted file
    REQUIRE(!result.has_value());
  }

  SECTION("M return no value W file contains module build_id with len > 512") {
    // Given a file that contains valid crash report data, but with one of the module
    // entries containing an excessively long path
    const auto build_id_pos = data.find("abc");
    REQUIRE(build_id_pos != std::string_view::npos);
    const std::string_view head{data.data(), build_id_pos - 8};
    const std::string_view tail{
        data.data() + build_id_pos + 3, data.size() - (build_id_pos + 3)
    };
    const uint64_t long_build_id_len = 520;
    const std::string len_bytes(
        reinterpret_cast<const char*>(&long_build_id_len), sizeof(long_build_id_len)
    );
    const std::string long_build_id(520, 'A');
    const std::string new_data =
        std::string(head) + len_bytes + long_build_id + std::string(tail);
    fs.Touch("crash", new_data);

    // And an open file handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get no value: the function aborts rather than potentially making a huge
    // heap allocation that might be the result of a malformed or misinterpreted file
    REQUIRE(!result.has_value());
  }
}
