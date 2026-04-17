// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/data/crash_context_write.hpp"

#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/storage/path.hpp"

#include "mock/filesystem.hpp"
#include "support/catch.hpp"
#include "support/crash_data.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("WriteCrashContext", "[unit][crash_reporting]") {
  // Given a mock filesystem
  MockFilesystem fs;

  // And file paths where we'll write <crash>.ctx and <crash>.ctx.tmp
  PlatformPath path;
  REQUIRE(path.Encode("crash.ctx"));
  PlatformPath tmp_path;
  REQUIRE(tmp_path.Encode("crash.ctx.tmp"));

  SECTION("M produce .ctx file with expected binary contents for latest format") {
    // Given the binary contents of the crash context file we expect to produce at the
    // current version
    const uint8_t* data_ptr = MOCK_CRASH_CONTEXT_V1;
    const size_t data_size = std::size(MOCK_CRASH_CONTEXT_V1);
    std::string_view data{reinterpret_cast<const char*>(data_ptr), data_size};

    // And the canonical context values that were used to come up with that binary data
    const RumFeatureContext rum_ctx{
        *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef"),
        *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef"),
        *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef"),
        *UUID::Parse("4c10171e-4334-4334-4334-b0000eeeefff")
    };

    // When we serialize our context to the mock file
    WriteCrashContext(fs, path, tmp_path, rum_ctx);

    // Then the file contains exactly the bytes we expect it to
    REQUIRE(fs.Cat("crash.ctx") == data);

    // And no temporary files are left behind
    REQUIRE(!fs.IsFile("crash.ctx.tmp"));
  }
}
