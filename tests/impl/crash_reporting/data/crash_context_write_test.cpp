// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_write.hpp"

#include "datadog/uuid.hpp"

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

  SECTION("M produce .ctx file with expected binary contents for latest format") {
    // Given the binary contents of the crash context file we expect to produce at the
    // current version
    std::string_view data = MOCK_CRASH_CONTEXT_V1.Get();

    // And the canonical context values that were used to come up with that binary data
    CrashContext ctx{};
    ctx.service = "mock-service";
    ctx.env = "mock-env";
    ctx.application_version = "1.2.3";
    ctx.variant = "Debug";
    ctx.source = "cpp";
    ctx.sdk_version = "2.0.0";
    ctx.tracking_consent = TrackingConsent::Pending;
    ctx.os_name = "mock-os";
    ctx.os_version = "2.3.4";
    ctx.os_build = "mock-build-number";
    ctx.os_version_major = "2";
    ctx.device_type = "desktop";
    ctx.device_name = "mock-device";
    ctx.device_model = "mock-model";
    ctx.device_brand = "mock-brand";
    ctx.device_architecture = "x86_64";
    ctx.device_locale = "en-US";
    ctx.device_time_zone = "America/New_York";
    ctx.user_id = "usr-123";
    ctx.user_name = "Alice";
    ctx.user_email = "alice@example.com";
    ctx.user_extra = Attribute::Object();
    ctx.account_id = "acct-456";
    ctx.account_name = "Acme Corp";
    ctx.account_extra = Attribute::Object();
    ctx.rum_session_state.application_id =
        *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
    ctx.rum_session_state.session_id =
        *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
    ctx.rum_session_state.is_sampled = true;
    ctx.rum_session_state.is_active = true;
    ctx.rum_session_state.is_initial_session = false;
    ctx.rum_session_state.has_tracked_any_view = true;
    ctx.rum_session_state.did_start_with_replay = false;
    ctx.last_view_event_json = R"({"type":"view"})";
    ctx.global_rum_attributes = Attribute::Object(1);
    ctx.global_rum_attributes.SetObjectProperty("plan", Attribute::String("gold"));

    // When we serialize it to the mock filesystem
    const bool ok = WriteCrashContext(fs, path, tmp_path, encode_buf, ctx);

    // Then serialization succeeds
    REQUIRE(ok);

    // And the file contains exactly the bytes we expect it to
    REQUIRE(fs.Cat("crash.ctx") == data);

    // And no temporary files are left behind
    REQUIRE(!fs.IsFile("crash.ctx.tmp"));
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
