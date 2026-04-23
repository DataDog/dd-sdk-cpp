// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "mock/filesystem.hpp"

using namespace datadog::impl;

TEST_CASE("File::Read retry behavior", "[unit][storage][filesystem]") {
  MockFilesystem fs;
  fs.Touch("/file", "hello world");
  auto wrapper = fs.Wrapper();

  SECTION("M read all requested bytes W content fits in one syscall") {
    // Given a file with known contents
    auto [open_result, file] = wrapper.OpenForRead("/file", false);
    REQUIRE(open_result == FilesystemResult::OK);

    // When we read exactly the file's content length with no read-size limit
    char buf[11];
    auto res = file.Read(buf, sizeof(buf));

    // Then we get all bytes in a single call
    REQUIRE(res.value == FilesystemResult::OK);
    REQUIRE(res.bytes_read == 11);
    REQUIRE(std::string(buf, 11) == "hello world");
  }

  SECTION("M assemble full result W underlying Read returns one byte at a time") {
    // Given a file with known contents and a mock that returns only 1 byte per call
    fs.SetMaxReadSize(1);
    auto [open_result, file] = wrapper.OpenForRead("/file", false);
    REQUIRE(open_result == FilesystemResult::OK);

    // When we request all 11 bytes
    char buf[11];
    auto res = file.Read(buf, sizeof(buf));

    // Then File::Read loops until all bytes are assembled
    REQUIRE(res.value == FilesystemResult::OK);
    REQUIRE(res.bytes_read == 11);
    REQUIRE(std::string(buf, 11) == "hello world");
  }

  SECTION("M report EOF W file is shorter than requested read size") {
    // Given a file with 11 bytes and a request for more than that
    auto [open_result, file] = wrapper.OpenForRead("/file", false);
    REQUIRE(open_result == FilesystemResult::OK);

    // When we request more bytes than the file contains
    char buf[20];
    auto res = file.Read(buf, sizeof(buf));

    // Then we get the bytes that were available and no error
    REQUIRE(res.value == FilesystemResult::OK);
    REQUIRE(res.bytes_read == 11);
    REQUIRE(std::string(buf, 11) == "hello world");
  }

  SECTION("M report EOF W file is shorter than requested read size with short reads") {
    // Given a file with 11 bytes, a request for 20 bytes, and a 3-byte read limit
    fs.SetMaxReadSize(3);
    auto [open_result, file] = wrapper.OpenForRead("/file", false);
    REQUIRE(open_result == FilesystemResult::OK);

    // When we request more bytes than the file contains
    char buf[20];
    auto res = file.Read(buf, sizeof(buf));

    // Then we accumulate all available bytes across multiple calls and report EOF
    REQUIRE(res.value == FilesystemResult::OK);
    REQUIRE(res.bytes_read == 11);
    REQUIRE(std::string(buf, 11) == "hello world");
  }

  SECTION("M propagate error W read fails on first call") {
    // Given a file that will fail on read
    fs.SimulateFailure(
        "/file", FilesystemResult::UnknownError, MockFilesystem::FailureFlags::IO
    );
    auto [open_result, file] = wrapper.OpenForRead("/file", false);
    REQUIRE(open_result == FilesystemResult::OK);

    // When we try to read
    char buf[11];
    auto res = file.Read(buf, sizeof(buf));

    // Then the error is propagated with 0 bytes read
    REQUIRE(res.value == FilesystemResult::UnknownError);
    REQUIRE(res.bytes_read == 0);
  }

  SECTION("M propagate error W read fails after partial read") {
    // Given a 1-byte-per-call read limit: read 3 bytes, then inject a failure
    fs.SetMaxReadSize(1);
    auto [open_result, file] = wrapper.OpenForRead("/file", false);
    REQUIRE(open_result == FilesystemResult::OK);

    // Read 3 bytes successfully first
    char buf[11];
    auto partial = file.Read(buf, 3);
    REQUIRE(partial.value == FilesystemResult::OK);
    REQUIRE(partial.bytes_read == 3);

    // Now inject a failure and attempt to read the rest
    fs.SimulateFailure(
        "/file", FilesystemResult::UnknownError, MockFilesystem::FailureFlags::IO
    );
    auto res = file.Read(buf + 3, 8);

    // Then the error is propagated with 0 bytes read from this second call
    REQUIRE(res.value == FilesystemResult::UnknownError);
    REQUIRE(res.bytes_read == 0);
  }
}
