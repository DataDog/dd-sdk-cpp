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

  SECTION(
      "M return OK with data with expected field values W file is a valid crash report"
  ) {
    // Given a file that contains our golden crash report data
    fs.Touch("crash", data);

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get a result value indicating that the file was parsed successfully
    REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::OK);
    REQUIRE(result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::OK);
    REQUIRE(result.empty == false);

    // And the header fields contain the values encoded in the mock binary data
    REQUIRE(result.data->fault_code == 11);  // SIGSEGV
    REQUIRE(result.data->fault_address == 0);
    REQUIRE(result.data->fault_flags == 0);
    REQUIRE(result.data->pid == 100);
    REQUIRE(result.data->tid == 101);
    REQUIRE(result.data->timestamp == 1700000000000ULL);

    // And there are two modules with the expected fields
    REQUIRE(result.data->modules.size() == 2);
    REQUIRE(result.data->modules[0].start_address == 0x100000);
    REQUIRE(result.data->modules[0].end_address == 0x200000);
    REQUIRE(result.data->modules[0].path == "/foo");
    REQUIRE(result.data->modules[0].build_id == "abc");
    REQUIRE(result.data->modules[1].start_address == 0x300000);
    REQUIRE(result.data->modules[1].end_address == 0x400000);
    REQUIRE(result.data->modules[1].path == "/bar");
    REQUIRE(result.data->modules[1].build_id == "");

    // And there are four stack frame addresses
    REQUIRE(result.data->stack_addresses.size() == 4);
    REQUIRE(result.data->stack_addresses[0] == 0x100100);
    REQUIRE(result.data->stack_addresses[1] == 0x100200);
    REQUIRE(result.data->stack_addresses[2] == 0x100300);
    REQUIRE(result.data->stack_addresses[3] == 0x100400);
  }

  SECTION("M return Empty with no data W file contains 0 bytes") {
    // Given a file that contains no data
    fs.Touch("crash", "");

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get a result indicating that the file was entirely empty, indicating that
    // we've successfully handled leftover a file which did not represent a crash
    REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::Empty);
    REQUIRE(!result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::OK);
    REQUIRE(result.empty == true);
  }

  SECTION("M return Malformed with no data W file has invalid header magic") {
    // Given a file that contains garbage data
    fs.Touch("crash", "hello-world-this-file-is-not-a-valid-crash-report");

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get a result value indicating that the file was not a valid crash report
    REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::Malformed);
    REQUIRE(!result.data.has_value());
  }

  SECTION("M return Malformed with no data W file has invalid footer magic") {
    // Given a file that contains valid crash report data, with the exception of having
    // garbage data in place of the final value
    const std::string_view data_less_8_bytes{data.data(), data.size() - 8};
    fs.Touch("crash", std::string(data_less_8_bytes) + "badfooter");

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get a result value indicating that the file was not a valid crash report
    REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::Malformed);
    REQUIRE(!result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::OK);
    REQUIRE(result.empty == false);
  }

  SECTION("M return Malformed W file is truncated") {
    // Given a variety of files that abruptly end at various points midway through the
    // data for a valid crash report file
    auto file_size = GENERATE(
        as<size_t>(),
        1,
        5,
        64,
        192,
        199,
        sizeof(MOCK_CRASH_REPORT_V1) - 8,
        sizeof(MOCK_CRASH_REPORT_V1) - 1
    );
    REQUIRE(file_size <= data.size());
    const std::string_view truncated_data{data.data(), file_size};
    fs.Touch("crash", truncated_data);

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse the file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get no value
    REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::Malformed);
    REQUIRE(!result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::OK);
    REQUIRE(result.empty == false);
  }

  SECTION(
      "M return Malformed with no data W file contains module path with len > 4096"
  ) {
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

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get no value: the function aborts rather than potentially making a huge
    // heap allocation that might be the result of a malformed or misinterpreted file
    REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::Malformed);
    REQUIRE(!result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::OK);
    REQUIRE(result.empty == false);
  }

  SECTION(
      "M return Malformed with no data W file contains module build_id with len > 512"
  ) {
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

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get no value: the function aborts rather than potentially making a huge
    // heap allocation that might be the result of a malformed or misinterpreted file
    REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::Malformed);
    REQUIRE(!result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::OK);
    REQUIRE(result.empty == false);
  }

  SECTION("M return Malformed W file contains more than 4096 loaded modules") {
    // Given two scenarios: one where our file will contain exactly the maximum number
    // of modules, and another where it exceeds the limit by 1
    auto target_module_count = GENERATE(as<size_t>(), 4096, 4097);

    // Given a file that contains valid crash report data, but an excessive number of
    // loaded module entries
    {
      // Find the offsets of the first and second module entry in our example file
      std::string_view magic{reinterpret_cast<const char*>(&CrashReportModuleMagic), 8};
      const auto first_module_pos = data.find(magic);
      REQUIRE(first_module_pos != std::string_view::npos);
      const auto second_module_pos = data.find(magic, first_module_pos + 8);
      REQUIRE(second_module_pos != std::string_view::npos);

      // Count any remaining modules in the original file, so we can populate the file
      // with exactly our desired number of modules
      size_t num_modules_in_file = 2;
      for (auto pos = second_module_pos + 8;
           (pos = data.find(magic, pos)) != std::string_view::npos;
           pos += 8) {
        ++num_modules_in_file;
      }
      REQUIRE(num_modules_in_file < target_module_count);

      // Extract the span of bytes representing the data for the first module entry
      const size_t first_module_size = second_module_pos - first_module_pos;
      std::string_view first_module{data.data() + first_module_pos, first_module_size};

      // Build a string that repeats that module N times
      const size_t num_extra_modules = target_module_count - num_modules_in_file;
      std::string extra_module_data;
      extra_module_data.reserve(first_module.size() * num_extra_modules);
      for (size_t i = 0; i < num_extra_modules; i++) {
        extra_module_data += first_module;
      }

      // Prepend that extra data to to the start of the file's module list, so that it
      // now has `num_extra_modules` additional modules, in addition to the entries that
      // already existed in the file
      std::string_view head{data.data(), first_module_pos};
      std::string_view tail{
          data.data() + first_module_pos, data.size() - first_module_pos
      };
      std::string new_data = std::string(head) + extra_module_data + std::string(tail);
      fs.Touch("crash", new_data);
    }

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then the result depends on whether we've exceeded the limit:
    if (target_module_count == 4096) {
      // At the limit: no issue; we get valid data
      REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::OK);
      REQUIRE(result.data.has_value());
      REQUIRE(result.data->modules.size() == 4096);
      REQUIRE(result.fs_result == FilesystemResult::OK);
      REQUIRE(result.empty == false);
    } else {
      // Over the limit: file is treated as malformed - the function aborts rather than
      // allowing our result value's modules vector to grow unbounded
      REQUIRE(target_module_count == 4097);
      REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::Malformed);
      REQUIRE(!result.data.has_value());
      REQUIRE(result.fs_result == FilesystemResult::OK);
      REQUIRE(result.empty == false);
    }
  }

  SECTION("M return Malformed W file contains more than 512 stack frames") {
    // Given two scenarios: one where our file will contain exactly the maximum number
    // of stack frames, and another where it exceeds the limit by 1
    auto target_frame_count = GENERATE(as<size_t>(), 512, 513);

    // Given a file that contains valid crash report data, but an excessive number of
    // stack frame entries
    {
      // Find the offsets of the first and second stack frame entry in our example file
      std::string_view magic{
          reinterpret_cast<const char*>(&CrashReportStackFrameMagic), 8
      };
      const auto first_frame_pos = data.find(magic);
      REQUIRE(first_frame_pos != std::string_view::npos);
      const auto second_frame_pos = data.find(magic, first_frame_pos + 8);
      REQUIRE(second_frame_pos != std::string_view::npos);

      // Count any remaining stack frames in the original file, so we can populate the
      // file with exactly our desired number of frames
      size_t num_frames_in_file = 2;
      for (auto pos = second_frame_pos + 8;
           (pos = data.find(magic, pos)) != std::string_view::npos;
           pos += 8) {
        ++num_frames_in_file;
      }
      REQUIRE(num_frames_in_file < target_frame_count);

      // Extract the span of bytes representing the data for the first stack frame entry
      const size_t first_frame_size = second_frame_pos - first_frame_pos;
      std::string_view first_frame{data.data() + first_frame_pos, first_frame_size};

      // Build a string that repeats that frame N times
      const size_t num_extra_frames = target_frame_count - num_frames_in_file;
      std::string extra_frame_data;
      extra_frame_data.reserve(first_frame.size() * num_extra_frames);
      for (size_t i = 0; i < num_extra_frames; i++) {
        extra_frame_data += first_frame;
      }

      // Prepend that extra data to the start of the file's stack frame list, so that
      // it now has `num_extra_frames` additional frames, in addition to the entries
      // that already existed in the file
      std::string_view head{data.data(), first_frame_pos};
      std::string_view tail{
          data.data() + first_frame_pos, data.size() - first_frame_pos
      };
      std::string new_data = std::string(head) + extra_frame_data + std::string(tail);
      fs.Touch("crash", new_data);
    }

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then the result depends on whether we've exceeded the limit:
    if (target_frame_count == 512) {
      // At the limit: no issue; we get valid data
      REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::OK);
      REQUIRE(result.data.has_value());
      REQUIRE(result.data->stack_addresses.size() == 512);
      REQUIRE(result.fs_result == FilesystemResult::OK);
      REQUIRE(result.empty == false);
    } else {
      // Over the limit: file is treated as malformed - the function aborts rather than
      // allowing our result value's stack_addresses vector to grow unbounded
      REQUIRE(target_frame_count == 513);
      REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::Malformed);
      REQUIRE(!result.data.has_value());
      REQUIRE(result.fs_result == FilesystemResult::OK);
      REQUIRE(result.empty == false);
    }
  }

  SECTION("M return ReadError W file can not be read due to filesystem error") {
    // Given a file that contains our valid crash report data, but that the filesystem
    // will prevent us from reading
    fs.Touch("crash", data);
    fs.SimulateFailure(
        "crash", FilesystemResult::PermissionDenied, MockFilesystem::FailureFlags::IO
    );

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashReport(open_res.file);

    // Then we get a result that indicates the filesystem error and contains no data
    REQUIRE(result.GetStatus() == ReadCrashReportResult::Status::ReadError);
    REQUIRE(!result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::PermissionDenied);
    REQUIRE(result.empty == false);
  }
}
