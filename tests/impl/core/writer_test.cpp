// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/writer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "datadog/impl/core/feature_read.hpp"
#include "datadog/impl/core/tlv.hpp"
#include "datadog/impl/platform/http.hpp"
#include "datadog/impl/storage/path.hpp"

#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("StringWriter", "[unit]") {
  SECTION("M write string to buffer W called as functor") {
    // Given a StringWriter initialized from a string of length 8
    std::string s{"deadbeef"};
    platform::HttpBodyWriter writer = StringWriter{s};

    // When the string is written to a buffer of size 8
    char buffer[8];
    const size_t num_bytes_written = writer(buffer, 8);

    // Then those 8 bytes are copied exactly
    REQUIRE(num_bytes_written == 8);
    REQUIRE(std::strstr(buffer, "deadbeef") == buffer);
  }

  SECTION("M not include null terminator W called as functor") {
    // Given a StringWriter initialized from a string of length 8
    std::string s{"deadbeef"};
    platform::HttpBodyWriter writer = StringWriter{s};

    // When the string is written to a buffer of size 9
    char buffer[9];
    std::memset(buffer, 'z', 9);
    const size_t num_bytes_written = writer(buffer, 9);

    // Then only 8 bytes are written, without null terminator
    REQUIRE(num_bytes_written == 8);
    REQUIRE(std::memcmp(buffer, "deadbeefz", 9) == 0);
  }

  SECTION("M write in multiple parts W buffer size is small") {
    // Given a StringWriter initialized from a string of length 8
    std::string s{"deadbeef"};
    platform::HttpBodyWriter writer = StringWriter{s};

    // When the string is written to a buffer given a dst size of 4
    char buffer[9];
    std::memset(buffer, '_', 9);
    size_t num_bytes_written = writer(buffer, 4);

    // Then only 4 bytes are written, without null terminator
    REQUIRE(num_bytes_written == 4);
    REQUIRE(std::memcmp(buffer, "dead_____", 9) == 0);

    // And: When another write is performed with the same size
    num_bytes_written = writer(buffer + 4, 4);

    // Then the writer picks up where it left off
    REQUIRE(num_bytes_written == 4);
    REQUIRE(std::memcmp(buffer, "deadbeef_", 9) == 0);
  }

  SECTION("M return EOF W finished writing") {
    // Given a StringWriter that's written all it can write
    std::string s{"deadbeef"};
    platform::HttpBodyWriter writer = StringWriter{s};
    char buffer[8];
    size_t num_bytes_written = writer(buffer, 8);
    REQUIRE(num_bytes_written == 8);

    // When write is called again
    num_bytes_written = writer(buffer, 8);

    // Then nothing is written, and 0 signals EOF
    REQUIRE(num_bytes_written == 0);
  }
}

TEST_CASE("TLVBatchWriter", "[unit][writer]") {
  // Prepare a mock filesystem with batch files for testing
  MockFilesystem fs;

  // Format unique JSON events, 8 bytes in length, e.g. '{"id":0}', and interleave
  // metadata blocks that just contain the ASCII bytes 'metadata'
  Block event_metadata{"metadata"};
  Block event_0{R"({"id":0})"};
  Block event_1{R"({"id":1})"};
  Block event_2{R"({"id":2})"};

  // three-events.dat contains three event blocks with JSON data, each prefaced by
  // a metadata block
  MockTLVFile mock_file = MockTLVFile(256)
                              .AppendMetadata(event_metadata)
                              .AppendEvent(event_0)
                              .AppendMetadata(event_metadata)
                              .AppendEvent(event_1)
                              .AppendMetadata(event_metadata)
                              .AppendEvent(event_2);
  mock_file.WriteTo(fs, "three-events.dat");

  // badfile.dat contains the same, but we'll corrupt it during the test so we need to
  // isolate it, because mock filesystem state is shared among all the tests below
  mock_file.WriteTo(fs, "badfile.dat");

  // trailing-metadata.dat contains the same, but it has an extra metadata block to
  // simulate an incomplete storage-thread write
  mock_file.AppendMetadata(event_metadata).WriteTo(fs, "trailing-metadata.dat");

  // only-metadata.dat is just three metadata blocks
  MockTLVFile(256)
      .AppendMetadata(event_metadata)
      .AppendMetadata(event_metadata)
      .AppendMetadata(event_metadata)
      .WriteTo(fs, "only-metadata.dat");

  // empty.dat has no event data; it's just an empty file
  fs.Touch("empty.dat", std::string_view{});

  // nontlv.dat is not a valid TLV file
  fs.Touch("nontlv.dat", "hello world");

  // Given that set of files, and a reusable buffer that our BatchReader instances
  // will use to store each block as it's read from the file
  std::vector<char> batch_reader_buffer;

  SECTION("M write concatenated TLV blocks W called as functor") {
    // Given a TLVBatchWriter initialized from an open batch file
    PlatformPath pp;
    pp.Encode("three-events.dat");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == FilesystemResult::OK);
    BatchReader reader{fs, handle, batch_reader_buffer};
    platform::HttpBodyWriter writer = TLVBatchWriter{reader};

    // When we demand the next (up to) 256 bytes
    char buffer[256];
    const size_t num_bytes_written = writer(buffer, 256);

    // Then a subsequent write call does nothing and returns 0
    REQUIRE(writer(buffer, sizeof(buffer)) == platform::HTTP_WRITE_RESULT_EOF);

    // And the writer formats all events into the buffer as a JSON array
    REQUIRE(num_bytes_written == 28);
    REQUIRE(
        std::string_view{buffer, num_bytes_written} == R"([{"id":0},{"id":1},{"id":2}])"
    );
    fs.Close(handle);
  }

  SECTION("M write in multiple parts W buffer size is small") {
    // Given a TLVBatchWriter initialized from an open batch file
    PlatformPath pp;
    pp.Encode("three-events.dat");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == FilesystemResult::OK);
    BatchReader reader{fs, handle, batch_reader_buffer};
    platform::HttpBodyWriter writer = TLVBatchWriter{reader};

    // When we demand the first 16 bytes
    char buffer[256];
    std::memset(buffer, '_', sizeof(buffer));
    size_t num_bytes_written = writer(buffer, 16);

    // Then exactly 16 bytes are written
    REQUIRE(num_bytes_written == 16);
    REQUIRE(std::memcmp(buffer, R"([{"id":0},{"id":________________)", 32) == 0);

    // And: When we demand the next 16 bytes
    num_bytes_written = writer(buffer + 16, 16);

    // Then a subsequent write call does nothing and returns 0
    REQUIRE(writer(buffer, sizeof(buffer)) == platform::HTTP_WRITE_RESULT_EOF);

    // And we should have the whole batch encoded
    REQUIRE(num_bytes_written == 12);
    REQUIRE(std::memcmp(buffer, R"([{"id":0},{"id":1},{"id":2}]____)", 32) == 0);
    fs.Close(handle);
  }

  SECTION("M encode properly W prefix, delimiter, and suffix are empty") {
    // Given a TLVBatchWriter initialized with empty strings for formatting
    PlatformPath pp;
    pp.Encode("three-events.dat");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == FilesystemResult::OK);
    BatchReader reader{fs, handle, batch_reader_buffer};
    platform::HttpBodyWriter writer = TLVBatchWriter{reader, "", "", ""};

    // When we demand the full contents 256 bytes
    char buffer[256];
    std::memset(buffer, '_', sizeof(buffer));
    const size_t num_bytes_written = writer(buffer, 256);

    // Then we get a plain string concatenation with no other formatting
    REQUIRE(num_bytes_written == 24);
    REQUIRE(std::memcmp(buffer, R"({"id":0}{"id":1}{"id":2}________)", 32) == 0);
    fs.Close(handle);
  }

  SECTION("M encode properly W prefix is empty and delimiter/suffix are multi-byte") {
    // Given a TLVBatchWriter configured to write CRLF-delimited lines
    const char* prefix = "";
    const char* delimiter = "\r\n";
    const char* suffix = "\r\nEND\r\n";
    PlatformPath pp;
    pp.Encode("three-events.dat");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == FilesystemResult::OK);
    BatchReader reader{fs, handle, batch_reader_buffer};
    platform::HttpBodyWriter writer = TLVBatchWriter{reader, prefix, delimiter, suffix};

    // And a write buffer
    char buffer[256];
    std::memset(buffer, '_', sizeof(buffer));

    // When we demand the contents one tiny chunk at a time
    size_t num_bytes_written = writer(buffer, 5);
    REQUIRE(num_bytes_written == 5);
    num_bytes_written = writer(buffer + 5, 5);
    REQUIRE(num_bytes_written == 5);
    num_bytes_written = writer(buffer + 10, 4);
    REQUIRE(num_bytes_written == 4);
    num_bytes_written = writer(buffer + 14, 5);
    REQUIRE(num_bytes_written == 5);
    num_bytes_written = writer(buffer + 19, 10);
    REQUIRE(num_bytes_written == 10);
    num_bytes_written = writer(buffer + 29, 5);
    REQUIRE(num_bytes_written == 5);
    num_bytes_written = writer(buffer + 34, 5);
    REQUIRE(num_bytes_written == 1);
    num_bytes_written = writer(buffer + 35, 5);
    REQUIRE(num_bytes_written == 0);

    // Then we should have all the data in our special format
    REQUIRE(
        std::memcmp(
            buffer, "{\"id\":0}\r\n{\"id\":1}\r\n{\"id\":2}\r\nEND\r\n_____", 40
        ) == 0
    );
    fs.Close(handle);
  }

  SECTION("M encode properly W a variety of write sizes are used") {
    // Given special formatting rules to wrap events in extra JSON
    const char* want =
        R"({"items":[{"type":1,"ref":{"id":0}},{"type":1,"ref":{"id":1}},{"type":1,"ref":{"id":2}}],"next":null})";
    const char* prefix = R"({"items":[{"type":1,"ref":)";
    const char* delimiter = R"(},{"type":1,"ref":)";
    const char* suffix = R"(}],"next":null})";

    // And a write buffer
    char buffer[256];

    // And a variety of different chunk sizes
    for (size_t size : {1, 2, 3, 4, 5, 7, 9, 15, 16, 17, 25, 35}) {
      // And a fresh TLVBatchWriter for each iteration
      PlatformPath pp;
      pp.Encode("three-events.dat");
      auto [open_result, handle] = fs.OpenForRead(pp, false);
      REQUIRE(open_result == FilesystemResult::OK);
      BatchReader reader{fs, handle, batch_reader_buffer};
      platform::HttpBodyWriter writer =
          TLVBatchWriter{reader, prefix, delimiter, suffix};

      // When we continually call the write func until it returns zero
      std::memset(buffer, '_', sizeof(buffer));
      size_t n = 0;
      while (size_t num_bytes_written = writer(buffer + n, size)) {
        n += num_bytes_written;
      }

      // Then we should have the correct result every time
      REQUIRE(n == std::strlen(want));
      REQUIRE(std::memcmp(buffer, want, sizeof(want) - 1) == 0);
      fs.Close(handle);
    }
  }

  SECTION("M abort request W file is empty") {
    // Given a TLVBatchWriter initialized from an empty file
    PlatformPath pp;
    pp.Encode("empty.dat");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == FilesystemResult::OK);
    BatchReader reader{fs, handle, batch_reader_buffer};
    platform::HttpBodyWriter writer = TLVBatchWriter{reader};

    // When we demand the contents
    char buffer[256];
    const size_t num_bytes_written = writer(buffer, 256);

    // Then the lack of any event data makes the batch invalid
    REQUIRE(num_bytes_written == platform::HTTP_WRITE_RESULT_ABORT);
    fs.Close(handle);
  }

  SECTION("M abort request W file contains only metadata blocks") {
    // Given a TLVBatchWriter initialized from a file with only metadata
    PlatformPath pp;
    pp.Encode("only-metadata.dat");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == FilesystemResult::OK);
    BatchReader reader{fs, handle, batch_reader_buffer};
    platform::HttpBodyWriter writer = TLVBatchWriter{reader};

    // When we demand the full contents
    char buffer[256];
    const size_t num_bytes_written = writer(buffer, 256);

    // Then lack of any event data makes the batch invalid
    REQUIRE(num_bytes_written == platform::HTTP_WRITE_RESULT_ABORT);
    fs.Close(handle);
  }

  SECTION("M abort request W file read fails due to IO error") {
    // Given a TLVBatchWriter initialized from a valid batch file
    PlatformPath pp;
    pp.Encode("badfile.dat");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == FilesystemResult::OK);
    BatchReader reader{fs, handle, batch_reader_buffer};
    platform::HttpBodyWriter writer = TLVBatchWriter{reader};

    // When we read successfully once
    char buffer[256];
    size_t num_bytes_written = writer(buffer, 16);
    REQUIRE(num_bytes_written == 16);

    // And then corrupt the file before the next write
    fs.Corrupt("badfile.dat");

    // Then the next write returns abort
    num_bytes_written = writer(buffer + 16, 16);
    REQUIRE(num_bytes_written == platform::HTTP_WRITE_RESULT_ABORT);
    fs.Close(handle);
  }

  SECTION("M abort request W file read fails due to invalid format") {
    // Given a TLVBatchWriter initialized from an invalid TLV file
    PlatformPath pp;
    pp.Encode("nontlv.dat");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == FilesystemResult::OK);
    BatchReader reader{fs, handle, batch_reader_buffer};
    platform::HttpBodyWriter writer = TLVBatchWriter{reader};

    // When we attempt to write
    char buffer[256];
    const size_t num_bytes_written = writer(buffer, 256);

    // Then the first call returns abort due to invalid format
    REQUIRE(num_bytes_written == platform::HTTP_WRITE_RESULT_ABORT);
    fs.Close(handle);
  }
}
