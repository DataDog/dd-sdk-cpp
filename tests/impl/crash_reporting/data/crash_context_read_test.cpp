// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_read.hpp"

#include "datadog/uuid.hpp"

#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/crash_reporting/data/crash_context_write.hpp"

#include "mock/filesystem.hpp"
#include "support/catch.hpp"
#include "support/crash_data.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("ReadCrashContext", "[unit][crash_reporting]") {
  // Given a mock filesystem
  MockFilesystem fs;

  // And the binary contents of an example crash context file
  const std::string_view data = MOCK_CRASH_CONTEXT_V1.Get();

  SECTION("M parse crash context file and return expected field values") {
    // Given a file that contains our golden crash context data
    fs.Touch("crash.ctx", data);

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash.ctx", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as crash context
    auto result = ReadCrashContext(open_res.file);

    // Then we get a valid result value
    REQUIRE(result.GetStatus() == ReadCrashContextResult::Status::OK);
    REQUIRE(result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::OK);

    // And all fields contain the values encoded in the mock data
    const CrashContext& got = *result.data;
    REQUIRE(got.service == "mock-service");
    REQUIRE(got.env == "mock-env");
    REQUIRE(got.application_version == "1.2.3");
    REQUIRE(got.source == "rum-cpp");
    REQUIRE(got.sdk_version == "2.0.0");
    REQUIRE(got.tracking_consent == datadog::TrackingConsent::Pending);
    REQUIRE(got.os_name == "mock-os");
    REQUIRE(got.os_version == "2.3.4");
    REQUIRE(got.os_build == "mock-build-number");
    REQUIRE(got.os_version_major == "2");
    REQUIRE(got.device_type == "desktop");
    REQUIRE(got.device_name == "mock-device");
    REQUIRE(got.device_model == "mock-model");
    REQUIRE(got.device_brand == "mock-brand");
    REQUIRE(got.device_architecture == "x86_64");
    REQUIRE(got.device_locale == "en-US");
    REQUIRE(got.device_time_zone == "America/New_York");
    REQUIRE(got.user_id == "usr-123");
    REQUIRE(got.user_name == "Alice");
    REQUIRE(got.user_email == "alice@example.com");
    REQUIRE(got.user_extra.GetType() == datadog::ValueType::Object);
    REQUIRE(got.user_extra.GetObjectPropertyCount() == 0);
    REQUIRE(got.account_id == "acct-456");
    REQUIRE(got.account_name == "Acme Corp");
    REQUIRE(got.account_extra.GetType() == datadog::ValueType::Object);
    REQUIRE(got.account_extra.GetObjectPropertyCount() == 0);
    REQUIRE(
        got.rum_session_state.session_id ==
        *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef")
    );
    REQUIRE(got.rum_session_state.is_sampled == true);
    REQUIRE(got.rum_session_state.is_active == true);
    REQUIRE(got.rum_session_state.is_initial_session == false);
    REQUIRE(got.rum_session_state.has_tracked_any_view == true);
    REQUIRE(got.last_view_event_json == R"({"type":"view"})");
    REQUIRE(got.global_rum_attributes.GetType() == datadog::ValueType::Object);
    REQUIRE(got.global_rum_attributes.GetObjectPropertyCount() == 1);
    REQUIRE(
        got.global_rum_attributes.GetObjectProperty("plan").GetStringValue() == "gold"
    );
  }

  SECTION("M return no value W file has invalid header magic") {
    // Given a file that contains garbage data
    fs.Touch("crash.ctx", "hello-world-this-file-is-not-a-valid-crash-report");

    // And an open handle to that file
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
    // Given a file that contains valid crash context data, with the exception of having
    // garbage data in place of the final value
    const std::string_view data_less_8_bytes{data.data(), data.size() - 8};
    fs.Touch("crash.ctx", std::string(data_less_8_bytes) + "badfooter");

    // And an open handle to that file
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

  SECTION("M return Malformed W file is truncated") {
    // Given a variety of files that abruptly end at various points midway through the
    // data for a valid crash context file
    auto file_size = GENERATE(
        as<size_t>(),
        1,
        5,
        64,
        // Omit the last 8 bytes (footer) or last 1 byte to ensure we detect truncation
        // at different depths
        sizeof(uint64_t) * 2  // valid header magic + version only
    );
    REQUIRE(file_size < data.size());
    const std::string_view truncated_data{data.data(), file_size};
    fs.Touch("crash.ctx", truncated_data);

    // And an open handle to that file
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

  SECTION("M return ReadError W file can not be read due to filesystem error") {
    // Given a file that contains our valid crash context data, but that the filesystem
    // will prevent us from reading
    fs.Touch("crash.ctx", data);
    fs.SimulateFailure(
        "crash.ctx",
        FilesystemResult::PermissionDenied,
        MockFilesystem::FailureFlags::IO
    );

    // And an open handle to that file
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash.ctx", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    // When we parse that file as a crash report
    auto result = ReadCrashContext(open_res.file);

    // Then we get a result that indicates the filesystem error and contains no data
    REQUIRE(result.GetStatus() == ReadCrashContextResult::Status::ReadError);
    REQUIRE(!result.data.has_value());
    REQUIRE(result.fs_result == FilesystemResult::PermissionDenied);
  }
}
