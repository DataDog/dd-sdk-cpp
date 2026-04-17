// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_read.hpp"

#include "datadog/uuid.hpp"

#include "datadog/impl/core/storage/filesystem_wrapper.hpp"

#include "mock/filesystem.hpp"
#include "support/catch.hpp"
#include "support/crash_data.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("ReadCrashContext", "[unit][crash_reporting]") {
  // Given a mock filesystem
  MockFilesystem fs;

  // And the binary contents of an example crash context file
  const std::string_view data{
      reinterpret_cast<const char*>(MOCK_CRASH_CONTEXT_V1),
      std::size(MOCK_CRASH_CONTEXT_V1)
  };

  SECTION("M parse crash context file and return expected field values") {
    // Given a file that contains our golden crash context data
    fs.Touch("crash.ctx", data);

    // And an open file handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash.ctx", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as crash context
    auto result = ReadCrashContext(open_res.file);

    // Then we get a valid result value
    REQUIRE(result.GetStatus() == ReadCrashContextResult::Status::OK);
    REQUIRE(result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::OK);

    // And all fields contain the values encoded in the mock binary data
    REQUIRE(
        result.data->rum_application_id ==
        *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef")
    );
    REQUIRE(
        result.data->rum_session_id ==
        *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef")
    );
    REQUIRE(
        result.data->rum_view_id == *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef")
    );
    REQUIRE(
        result.data->rum_action_id ==
        *UUID::Parse("4c10171e-4334-4334-4334-b0000eeeefff")
    );
  }

  SECTION("M return no value W file has invalid header magic") {
    // Given a file that contains garbage data
    fs.Touch("crash.ctx", "hello-world-this-file-is-not-a-valid-crash-report");

    // And an open file handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash.ctx", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as crash context
    auto result = ReadCrashContext(open_res.file);
    REQUIRE(result.fs_result == FilesystemResult::OK);

    // Then we get no value
    REQUIRE(result.GetStatus() == ReadCrashContextResult::Status::Malformed);
    REQUIRE(!result.data.has_value());
  }

  SECTION("M return no value W file has invalid footer magic") {
    // Given a file that contains valid crash context data, with the exception of a
    // missing footer
    const std::string_view data_less_8_bytes{data.data(), data.size() - 8};
    fs.Touch("crash.ctx", data_less_8_bytes);

    // And an open file handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash.ctx", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as crash context
    auto result = ReadCrashContext(open_res.file);

    // Then we get no value
    REQUIRE(!result.data.has_value());
    REQUIRE(result.GetStatus() == ReadCrashContextResult::Status::Malformed);
    REQUIRE(result.fs_result == FilesystemResult::OK);
  }
}
