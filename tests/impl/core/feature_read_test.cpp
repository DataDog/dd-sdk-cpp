// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/feature_read.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <vector>

#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"
#include "support/diagnostics.hpp"

using namespace datadog::impl;

TEST_CASE("BatchReader", "[unit]") {
  // Given a mock filesystem on which we can open arbitrary files
  MockFilesystem fs;
  auto fs_open = [&fs](const char* path) {
    auto res = fs.Wrapper().OpenForRead(path, false);
    REQUIRE(res.value == FilesystemResult::OK);
    return std::move(res.file);
  };

  // And a diagnostic logger that will capture all messages emitted
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  SECTION("M return Success and read event blocks W file is valid") {
    // Given a mock file with two valid TLV event blocks
    MockTLVFile()
        .AppendEvent(Block{"Hello"})
        .AppendEvent(Block{"hi"})
        .WriteTo(fs, "hello.dat");

    // And a reusable read buffer
    std::vector<char> buffer;

    // And a BatchReader initialized to read from that file into that buffer
    BatchReader reader(logger, fs_open("hello.dat"), buffer);

    // When we read the first block
    auto block_0_res = reader.ReadNext();

    // Then we should get the data for that block
    REQUIRE(block_0_res.status == BatchReader::Result::Status::Success);
    REQUIRE(block_0_res.block.type == TLVBlockType::Event);
    REQUIRE(block_0_res.block.data == "Hello");

    // And our buffer should be used as the underlying storage for that block data
    REQUIRE(buffer.capacity() >= 5);
    REQUIRE(std::string_view{buffer.data(), 5} == "Hello");

    // And: When we read the next block
    auto block_1_res = reader.ReadNext();

    // Then we should get the data for that block
    REQUIRE(block_1_res.status == BatchReader::Result::Status::Success);
    REQUIRE(block_1_res.block.type == TLVBlockType::Event);
    REQUIRE(block_1_res.block.data == "hi");

    // And the data should be written to the same reusable buffer
    REQUIRE(buffer.capacity() >= 2);
    REQUIRE(std::string_view{buffer.data(), 2} == "hi");

    // And: When we attempt to read the next block
    auto block_2_res = reader.ReadNext();

    // Then we get EOF since we've reached the end of the file
    REQUIRE(block_2_res.status == BatchReader::Result::Status::EndOfFile);

    // And no diagnostic messages were logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION(
      "M return Success and read all blocks W file contains metadata + event blocks"
  ) {
    // Given a mock file with two valid TLV event blocks
    MockTLVFile()
        .AppendMetadata(Block{"metadata-0"})
        .AppendEvent(Block{"event-0"})
        .AppendMetadata(Block{"metadata-1"})
        .AppendEvent(Block{"event-1"})
        .WriteTo(fs, "foo");

    // And a BatchReader
    std::vector<char> buffer;
    BatchReader reader(logger, fs_open("foo"), buffer);

    // When we read until EOF and concatenate everything into a string
    std::string s;
    s.reserve(128);
    for (int i = 0; i < 4; i++) {
      auto block_res = reader.ReadNext();

      // Then we get the expected results from each read
      REQUIRE(block_res.status == BatchReader::Result::Status::Success);
      REQUIRE(
          block_res.block.type ==
          (i % 2 == 0 ? TLVBlockType::Metadata : TLVBlockType::Event)
      );
      s += block_res.block.data;
    }

    // And reading once more would give us EOF
    auto res = reader.ReadNext();
    REQUIRE(res.status == BatchReader::Result::Status::EndOfFile);

    // And we have the expected data once we're done reading
    REQUIRE(s == "metadata-0event-0metadata-1event-1");

    // And no diagnostic messages were logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }

  SECTION("M return Error W file read fails due to filesystem error") {
    // Given a mock file with two valid TLV event blocks
    MockTLVFile()
        .AppendEvent(Block{"event-0"})
        .AppendEvent(Block{"event-1"})
        .WriteTo(fs, "foo");

    // And a BatchReader
    std::vector<char> buffer;
    BatchReader reader(logger, fs_open("foo"), buffer);

    // When we read the first block under normal conditions
    auto block_0_res = reader.ReadNext();

    // Then we get the data for that block
    REQUIRE(block_0_res.status == BatchReader::Result::Status::Success);
    REQUIRE(block_0_res.block.type == TLVBlockType::Event);
    REQUIRE(block_0_res.block.data == "event-0");

    // Next: Given external conditions that prevent file reads
    fs.SimulateFailure(
        "foo", FilesystemResult::UnknownError, MockFilesystem::FailureFlags::IO
    );

    // When we attempt to read the next block
    auto block_1_res = reader.ReadNext();

    // Then we get an error indicating the file couldn't be read
    REQUIRE(block_1_res.status == BatchReader::Result::Status::Error);

    // And a diagnostic warning was logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 1);
  }

  SECTION("M return Error W file contains a TLV header w/o data") {
    // Given a mock file with a header that indicates 32 bytes of data to follow,
    // but no actual data after the header
    MockTLVFile()
        .AppendHeader(impl::TLVBlockHeader{TLVBlockType::Event, 32})
        .WriteTo(fs, "foo");
    std::vector<char> buffer;
    BatchReader reader(logger, fs_open("foo"), buffer);

    // When we attempt to read the next block
    auto block_res = reader.ReadNext();

    // Then the read is unsuccessful
    REQUIRE(block_res.status == BatchReader::Result::Status::Error);

    // And a diagnostic warning was logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 1);
  }

  SECTION("M return Error W file has TLV header with invalid block type") {
    // Given a mock file containing an otherwise well-formed TLV block that encodes
    // the block type with a value that does not correspond to a known TLVBlockType
    MockTLVFile()
        .AppendHeader(impl::TLVBlockHeader{static_cast<TLVBlockType>(0x0002), 7})
        .AppendBytes("event-0")
        .WriteTo(fs, "foo");
    std::vector<char> buffer;
    BatchReader reader(logger, fs_open("foo"), buffer);

    // When we attempt to read from that file
    auto block_res = reader.ReadNext();

    // Then the read is unsuccessful
    REQUIRE(block_res.status == BatchReader::Result::Status::Error);

    // And a diagnostic warning was logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 1);
  }

  SECTION("M return Error W file has TLV header indicating zero size") {
    // Given a mock file containing an otherwise well-formed TLV block that shows a
    // length of zero for its value
    MockTLVFile()
        .AppendHeader(impl::TLVBlockHeader{TLVBlockType::Event, 0})
        .AppendBytes("event-0")
        .WriteTo(fs, "foo");
    std::vector<char> buffer;
    BatchReader reader(logger, fs_open("foo"), buffer);

    // When we attempt to read from that file
    auto block_res = reader.ReadNext();

    // Then the read is unsuccessful
    REQUIRE(block_res.status == BatchReader::Result::Status::Error);

    // And a diagnostic warning was logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 1);
  }

  SECTION("M return Error W file contains non-TLV data") {
    // Given a mock file that does not contain valid TLV data
    fs.Touch("foo", "this is not TLV-encoded binary data");
    std::vector<char> buffer;
    BatchReader reader(logger, fs_open("foo"), buffer);

    // When we attempt to read from that ifle
    auto block_res = reader.ReadNext();

    // Then the read is unsuccessful
    REQUIRE(block_res.status == BatchReader::Result::Status::Error);

    // And a diagnostic warning was logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 1);
  }

  SECTION("M return EndOfFile W file is empty") {
    // Given a mock file that is entirely empty
    fs.Touch("foo");
    std::vector<char> buffer;
    BatchReader reader(logger, fs_open("foo"), buffer);

    // When we attempt to read from that ifle
    auto block_res = reader.ReadNext();

    // Then we get a result indicating we've reached EOF
    REQUIRE(block_res.status == BatchReader::Result::Status::EndOfFile);
  }

  SECTION("M reuse buffer W multiple reads") {
    // Given a bunch of large-ish string values that repeat the same byte N times
    std::string value_a_1024(1024, 'a');
    std::string value_b_768(768, 'b');
    std::string value_c_16380(16380, 'c');
    std::string value_d_16384(16384, 'd');

    // And a reusable buffer in which we've initially reserved 256 bytes
    std::vector<char> buffer;
    buffer.reserve(256);

    // And a mock file that contains valid TLV event blocks for each string
    MockTLVFile(4096)
        .AppendEvent(value_a_1024)
        .AppendEvent(value_b_768)
        .AppendEvent(value_c_16380)
        .AppendEvent(value_d_16384)
        .WriteTo(fs, "foo");

    // And a reader initialized to use our buffer
    BatchReader reader(logger, fs_open("foo"), buffer);
    REQUIRE(buffer.capacity() == 256);

    // When we read the block with 'a' x 1024
    auto block_res = reader.ReadNext();
    REQUIRE(block_res.status == BatchReader::Result::Status::Success);
    REQUIRE(block_res.block.type == TLVBlockType::Event);
    REQUIRE(block_res.block.data.size() == 1024);
    REQUIRE(
        std::count(block_res.block.data.begin(), block_res.block.data.end(), 'a') ==
        1024
    );

    // And we read the block with 'b' x 768
    block_res = reader.ReadNext();
    REQUIRE(block_res.status == BatchReader::Result::Status::Success);
    REQUIRE(block_res.block.type == TLVBlockType::Event);
    REQUIRE(block_res.block.data.size() == 768);
    REQUIRE(
        std::count(block_res.block.data.begin(), block_res.block.data.end(), 'b') == 768
    );

    // Then the underlying buffer should have been reallocated to fit at least 1024
    // bytes, and block B should have overwritten the first 768 bytes of block A
    REQUIRE(buffer.size() == 768);
    REQUIRE(buffer.capacity() >= 1024);
    REQUIRE(std::count(buffer.begin(), buffer.end(), 'b') == 768);

    // And the buffer's allocation strategy should be conservative enough not to
    // jump straight to the order of ~16kb when we're working with ~1kb values
    // Note: This is an implementation detail, but a fair assumption to codify
    REQUIRE(buffer.capacity() < 16384);

    // Next: When we read the block with 'c' x 16380
    block_res = reader.ReadNext();
    REQUIRE(block_res.status == BatchReader::Result::Status::Success);
    REQUIRE(block_res.block.type == TLVBlockType::Event);
    REQUIRE(block_res.block.data.size() == 16380);
    REQUIRE(
        std::count(block_res.block.data.begin(), block_res.block.data.end(), 'c') ==
        16380
    );

    // Then the buffer's allocation strategy should be smart enough to jump to the
    // next power of two, which is only a few bytes away (this also an
    // implementation detail; see QuantizeBufferSize)
    const size_t capacity_c = buffer.capacity();
    REQUIRE(capacity_c >= 16384);

    // Next: When we read the block with 'd' x 16384
    block_res = reader.ReadNext();
    REQUIRE(block_res.status == BatchReader::Result::Status::Success);
    REQUIRE(block_res.block.type == TLVBlockType::Event);
    REQUIRE(block_res.block.data.size() == 16384);
    REQUIRE(
        std::count(block_res.block.data.begin(), block_res.block.data.end(), 'd') ==
        16384
    );

    // Then the buffer should not have been reallocated due to the tiny difference
    // in size (another implementation detail)
    REQUIRE(buffer.capacity() == capacity_c);

    // And one final read should hit EOF
    block_res = reader.ReadNext();
    REQUIRE(block_res.status == BatchReader::Result::Status::EndOfFile);

    // And no diagnostic messages were logged
    REQUIRE(diagnostics.error.size() == 0);
    REQUIRE(diagnostics.warning.size() == 0);
  }
}
