// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_write.hpp"

#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/crash_reporting/data/crash_context_read.hpp"

#include "mock/filesystem.hpp"
#include "support/catch.hpp"
#include "support/crash_data.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("WriteCrashContext", "[unit][crash_reporting]") {
  // Given a mock filesystem
  MockFilesystem fs;

  // And a reusable encode buffer (caller-owned, as required by WriteCrashContext)
  std::vector<char> encode_buf;

  // And file paths where we'll write <crash>.ctx and <crash>.ctx.tmp
  PlatformPath path;
  REQUIRE(path.Encode("crash.ctx"));
  PlatformPath tmp_path;
  REQUIRE(tmp_path.Encode("crash.ctx.tmp"));

  SECTION("M produce .ctx file that round-trips correctly") {
    // Given a fully-populated CrashContext
    const CrashContext ctx = MakeMockCrashContext();

    // When we serialize it to the mock filesystem
    const bool ok = WriteCrashContext(fs, path, tmp_path, encode_buf, ctx);

    // Then serialization succeeds
    REQUIRE(ok);

    // And no temporary files are left behind
    REQUIRE(!fs.IsFile("crash.ctx.tmp"));

    // And the file can be read back via ReadCrashContext with all fields intact
    const bool hold_advisory_lock = false;
    auto open_res = fs.Wrapper().OpenForRead("crash.ctx", hold_advisory_lock);
    REQUIRE(open_res.value == FilesystemResult::OK);

    auto result = ReadCrashContext(open_res.file);
    REQUIRE(result.GetStatus() == ReadCrashContextResult::Status::OK);
    REQUIRE(result.data.has_value());

    const CrashContext& got = *result.data;
    REQUIRE(got.service == ctx.service);
    REQUIRE(got.env == ctx.env);
    REQUIRE(got.application_version == ctx.application_version);
    REQUIRE(got.source == ctx.source);
    REQUIRE(got.sdk_version == ctx.sdk_version);
    REQUIRE(got.tracking_consent == ctx.tracking_consent);
    REQUIRE(got.os_name == ctx.os_name);
    REQUIRE(got.os_version == ctx.os_version);
    REQUIRE(got.os_build == ctx.os_build);
    REQUIRE(got.os_version_major == ctx.os_version_major);
    REQUIRE(got.device_type == ctx.device_type);
    REQUIRE(got.device_name == ctx.device_name);
    REQUIRE(got.device_model == ctx.device_model);
    REQUIRE(got.device_brand == ctx.device_brand);
    REQUIRE(got.device_architecture == ctx.device_architecture);
    REQUIRE(got.device_locale == ctx.device_locale);
    REQUIRE(got.device_time_zone == ctx.device_time_zone);
    REQUIRE(got.user_id == ctx.user_id);
    REQUIRE(got.user_name == ctx.user_name);
    REQUIRE(got.user_email == ctx.user_email);
    REQUIRE(got.user_extra.GetType() == datadog::ValueType::Object);
    REQUIRE(got.user_extra.GetObjectPropertyCount() == 0);
    REQUIRE(got.account_id == ctx.account_id);
    REQUIRE(got.account_name == ctx.account_name);
    REQUIRE(got.account_extra.GetType() == datadog::ValueType::Object);
    REQUIRE(got.account_extra.GetObjectPropertyCount() == 0);
    REQUIRE(got.rum_session_state.session_id == ctx.rum_session_state.session_id);
    REQUIRE(got.rum_session_state.is_sampled == ctx.rum_session_state.is_sampled);
    REQUIRE(got.rum_session_state.is_active == ctx.rum_session_state.is_active);
    REQUIRE(
        got.rum_session_state.is_initial_session ==
        ctx.rum_session_state.is_initial_session
    );
    REQUIRE(
        got.rum_session_state.has_tracked_any_view ==
        ctx.rum_session_state.has_tracked_any_view
    );
    REQUIRE(got.last_view_event_json == ctx.last_view_event_json);
    REQUIRE(got.global_rum_attributes.GetType() == datadog::ValueType::Object);
    REQUIRE(
        got.global_rum_attributes.GetObjectProperty("plan").GetStringValue() == "gold"
    );
  }

  SECTION("M fail W .ctx.tmp file can not be opened") {
    // Given a filesystem that won't permit us to open crash.ctx.tmp for write
    fs.Touch("crash.ctx", "foo");
    fs.Touch("crash.ctx.tmp", "bar");
    fs.SimulateFailure(
        "crash.ctx.tmp",
        FilesystemResult::UnknownError,
        MockFilesystem::FailureFlags::Open
    );

    // When we attempt to serialize any context using that path
    const bool ok = WriteCrashContext(fs, path, tmp_path, encode_buf, CrashContext{});

    // Then serialization fails
    REQUIRE(!ok);

    // And our original files are untouched
    REQUIRE(fs.Cat("crash.ctx") == "foo");
    REQUIRE(fs.Cat("crash.ctx.tmp") == "bar");
  }

  SECTION("M fail W .ctx.tmp file can not be written to") {
    // Given a filesystem that will let us overwrite crash.ctx.tmp but will cause write
    // operations to fail
    fs.Touch("crash.ctx", "foo");
    fs.Touch("crash.ctx.tmp", "bar");
    fs.SimulateFailure(
        "crash.ctx.tmp",
        FilesystemResult::UnknownError,
        MockFilesystem::FailureFlags::IO
    );

    // When we attempt to serialize any context using that path
    const bool ok = WriteCrashContext(fs, path, tmp_path, encode_buf, CrashContext{});

    // Then serialization fails
    REQUIRE(!ok);

    // And our now-truncated .tmp file is deleted
    REQUIRE(!fs.IsFile("crash.ctx.tmp"));

    // And our original crash context file remains
    REQUIRE(fs.Cat("crash.ctx") == "foo");
  }

  SECTION("M fail W .ctx file can not be overwritten") {
    // Given a filesystem where we can freely write .ctx.tmp, but where the original
    // .ctx file will refuse to be overwritten
    fs.Touch("crash.ctx", "foo");
    fs.SimulateFailure(
        "crash.ctx",
        FilesystemResult::UnknownError,
        MockFilesystem::FailureFlags::Rename
    );

    // When we attempt to serialize any context using that path
    const bool ok = WriteCrashContext(fs, path, tmp_path, encode_buf, CrashContext{});

    // Then serialization fails
    REQUIRE(!ok);

    // And our now-abandoned .tmp file is deleted
    REQUIRE(!fs.IsFile("crash.ctx.tmp"));

    // And our original crash context file remains
    REQUIRE(fs.Cat("crash.ctx") == "foo");
  }
}
