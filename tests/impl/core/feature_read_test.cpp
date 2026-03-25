// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/feature_read.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <vector>

#include "datadog/impl/storage/path.hpp"

#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"

using namespace datadog::impl;

TEST_CASE("BatchReader", "[unit]") {
  SECTION("M read event blocks W file is valid") {
    // Given a mock file with two valid TLV event blocks
    MockFilesystem fs;
    MockTLVFile()
        .AppendEvent(Block{"Hello"})
        .AppendEvent(Block{"hi"})
        .WriteTo(fs, "hello.dat");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("hello.dat"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // And a reusable read buffer
    std::vector<char> buffer;

    // And a BatchReader initialized to read from that file into that buffer
    BatchReader reader(fs, handle, buffer);

    // When we read the first block
    auto block_0_res = reader.ReadNext();

    // Then we should get the data for that block
    REQUIRE(block_0_res.has_value());
    std::optional<TLVBlock> block_0 = *block_0_res;
    REQUIRE(block_0.has_value());
    REQUIRE(block_0->type == TLVBlockType::Event);
    REQUIRE(block_0->data == "Hello");

    // And our buffer should be used as the underlying storage for that block data
    REQUIRE(buffer.capacity() >= 5);
    REQUIRE(std::string_view{buffer.data(), 5} == "Hello");

    // And: When we read the next block
    auto block_1_res = reader.ReadNext();

    // Then we should get the data for that block
    REQUIRE(block_1_res.has_value());
    std::optional<TLVBlock> block_1 = *block_1_res;
    REQUIRE(block_1->type == TLVBlockType::Event);
    REQUIRE(block_1->data == "hi");

    // And the data should be written to the same reusable buffer
    REQUIRE(buffer.capacity() >= 2);
    REQUIRE(std::string_view{buffer.data(), 2} == "hi");

    // And: When we attempt to read the next block
    auto block_2_res = reader.ReadNext();

    // Then we get nullopt instead of a block (with no error), since we're at EOF
    REQUIRE(block_2_res.has_value());
    std::optional<TLVBlock> block_2 = *block_2_res;
    REQUIRE(!block_2.has_value());

    fs.Close(handle);
  }

  SECTION("M read all blocks W file contains metadata + event blocks") {
    // Given a mock file with two valid TLV event blocks
    MockFilesystem fs;
    MockTLVFile()
        .AppendMetadata(Block{"metadata-0"})
        .AppendEvent(Block{"event-0"})
        .AppendMetadata(Block{"metadata-1"})
        .AppendEvent(Block{"event-1"})
        .WriteTo(fs, "foo");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("foo"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // And a BatchReader
    std::vector<char> buffer;
    BatchReader reader(fs, handle, buffer);

    // When we read until EOF and concatenate everything into a string
    std::string s;
    s.reserve(128);
    for (int i = 0; i < 4; i++) {
      auto block_res = reader.ReadNext();

      // Then we get the expected results from each read
      REQUIRE(block_res.has_value());
      auto block = *block_res;
      REQUIRE(block.has_value());
      REQUIRE(
          block->type == (i % 2 == 0 ? TLVBlockType::Metadata : TLVBlockType::Event)
      );
      s += block->data;
    }

    // And reading once more would give us nullopt to indicate EOF
    auto res = reader.ReadNext();
    REQUIRE(res.has_value());
    REQUIRE(*res == std::nullopt);

    // And we have the expected data once we're done reading
    REQUIRE(s == "metadata-0event-0metadata-1event-1");

    fs.Close(handle);
  }

  SECTION("M return IOError W file read fails due to low-level filesystem error") {
    // Given a mock file with two valid TLV event blocks
    MockFilesystem fs;
    MockTLVFile()
        .AppendEvent(Block{"event-0"})
        .AppendEvent(Block{"event-1"})
        .WriteTo(fs, "foo");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("foo"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // And a BatchReader
    std::vector<char> buffer;
    BatchReader reader(fs, handle, buffer);

    // When we read the first block under normal conditions
    auto block_0_res = reader.ReadNext();

    // Then we get the data for that block
    REQUIRE(block_0_res.has_value());
    auto block_0 = *block_0_res;
    REQUIRE(block_0->type == TLVBlockType::Event);
    REQUIRE(block_0->data == "event-0");

    // Next: Given external conditions that prevent file reads
    fs.Corrupt("foo");

    // When we attempt to read the next block
    auto block_1_res = reader.ReadNext();

    // Then we get an error indicating the file couldn't be read
    REQUIRE(!block_1_res.has_value());
    REQUIRE(block_1_res.error() == BatchReadError::IOError);

    fs.Close(handle);
  }

  SECTION("M return FailedRead W file read fails due to invalid file state") {
    // Given a mock file with two valid TLV event blocks
    MockFilesystem fs;
    MockTLVFile()
        .AppendEvent(Block{"event-0"})
        .AppendEvent(Block{"event-1"})
        .WriteTo(fs, "foo");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("foo"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    std::vector<char> buffer;
    BatchReader reader(fs, handle, buffer);

    // And normal conditions that have allowed us to read the first block
    auto block_0_res = reader.ReadNext();
    REQUIRE(block_0_res.has_value());
    auto block_0 = *block_0_res;
    REQUIRE(block_0->type == TLVBlockType::Event);
    REQUIRE(block_0->data == "event-0");

    // When we attempt a read operation that will fail due to issues with our handle
    fs.SetFail("foo", true);
    auto block_1_res = reader.ReadNext();

    // Then we get an error indicating the read operation failed
    REQUIRE(!block_1_res.has_value());
    REQUIRE(block_1_res.error() == BatchReadError::FailedRead);

    fs.Close(handle);
  }

  SECTION("M return InvalidBlockFormat W file contains a TLV header w/o data") {
    // Given a mock file with a header that indicates 32 bytes of data to follow,
    // but no actual data after the header
    MockFilesystem fs;
    MockTLVFile()
        .AppendHeader(impl::TLVBlockHeader{TLVBlockType::Event, 32})
        .WriteTo(fs, "foo");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("foo"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    std::vector<char> buffer;
    BatchReader reader(fs, handle, buffer);

    // When we attempt to read the next block
    auto block_res = reader.ReadNext();

    // Then we get an error indicating the file contents are not valid TLV
    REQUIRE(!block_res.has_value());
    REQUIRE(block_res.error() == BatchReadError::InvalidBlockFormat);

    fs.Close(handle);
  }

  SECTION("M return InvalidBlockFormat W file has unrecognized TLV block type") {
    // Given a mock file containing an otherwise well-formed TLV block that encodes
    // the block type with a value that does not correspond to a known TLVBlockType
    MockFilesystem fs;
    MockTLVFile()
        .AppendHeader(impl::TLVBlockHeader{static_cast<TLVBlockType>(0x0002), 7})
        .AppendBytes("event-0")
        .WriteTo(fs, "foo");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("foo"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    std::vector<char> buffer;
    BatchReader reader(fs, handle, buffer);

    // When we attempt to read from that file
    auto block_res = reader.ReadNext();

    // Then we get an error indicating the file contents are not valid TLV
    REQUIRE(!block_res.has_value());
    REQUIRE(block_res.error() == BatchReadError::InvalidBlockFormat);

    fs.Close(handle);
  }

  SECTION("M return InvalidBlockFormat W file has TLV header indicating zero size") {
    // Given a mock file containing an otherwise well-formed TLV block that shows a
    // length of zero for its value
    MockFilesystem fs;
    MockTLVFile()
        .AppendHeader(impl::TLVBlockHeader{TLVBlockType::Event, 0})
        .AppendBytes("event-0")
        .WriteTo(fs, "foo");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("foo"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    std::vector<char> buffer;
    BatchReader reader(fs, handle, buffer);

    // When we attempt to read from that file
    auto block_res = reader.ReadNext();

    // Then we get an error indicating the file contents are not valid TLV
    REQUIRE(!block_res.has_value());
    REQUIRE(block_res.error() == BatchReadError::InvalidBlockFormat);

    fs.Close(handle);
  }

  SECTION("M return InvalidBlockFormat W file contains non-TLV data") {
    // Given a mock file that does not contain valid TLV data
    MockFilesystem fs;
    fs.Touch("foo", "this is not TLV-encoded binary data");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("foo"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    std::vector<char> buffer;
    BatchReader reader(fs, handle, buffer);

    // When we attempt to read from that file
    auto block_res = reader.ReadNext();

    // Then we get an error indicating the file contents are not valid TLV
    REQUIRE(!block_res.has_value());
    REQUIRE(block_res.error() == BatchReadError::InvalidBlockFormat);

    fs.Close(handle);
  }

  SECTION("M return nullopt W file is empty") {
    // Given a mock file that is entirely empty
    MockFilesystem fs;
    fs.Touch("foo", "");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("foo"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    std::vector<char> buffer;
    BatchReader reader(fs, handle, buffer);

    // When we attempt to read from that file
    auto block_res = reader.ReadNext();

    // Then we get a successful read result with a nullopt value, indicating that we've
    // reached EOF
    REQUIRE(block_res.has_value());
    REQUIRE(*block_res == std::nullopt);

    fs.Close(handle);
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
    MockFilesystem fs;
    MockTLVFile(4096)
        .AppendEvent(value_a_1024)
        .AppendEvent(value_b_768)
        .AppendEvent(value_c_16380)
        .AppendEvent(value_d_16384)
        .WriteTo(fs, "foo");

    impl::PlatformPath pp;
    REQUIRE(pp.Encode("foo"));
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // And a reader initialized to use our buffer
    BatchReader reader(fs, handle, buffer);
    REQUIRE(buffer.capacity() == 256);

    // When we read the block with 'a' x 1024
    auto block_res = reader.ReadNext();
    REQUIRE(block_res.has_value());
    auto block = *block_res;
    REQUIRE(block->type == TLVBlockType::Event);
    REQUIRE(block->data.size() == 1024);
    REQUIRE(std::count(block->data.begin(), block->data.end(), 'a') == 1024);

    // And we read the block with 'b' x 768
    block_res = reader.ReadNext();
    REQUIRE(block.has_value());
    block = *block_res;
    REQUIRE(block->type == TLVBlockType::Event);
    REQUIRE(block->data.size() == 768);
    REQUIRE(std::count(block->data.begin(), block->data.end(), 'b') == 768);

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
    REQUIRE(block.has_value());
    block = *block_res;
    REQUIRE(block->type == TLVBlockType::Event);
    REQUIRE(block->data.size() == 16380);
    REQUIRE(std::count(block->data.begin(), block->data.end(), 'c') == 16380);

    // Then the buffer's allocation strategy should be smart enough to jump to the
    // next power of two, which is only a few bytes away (this also an
    // implementation detail; see QuantizeBufferSize)
    const size_t capacity_c = buffer.capacity();
    REQUIRE(capacity_c >= 16384);

    // Next: When we read the block with 'd' x 16384
    block_res = reader.ReadNext();
    REQUIRE(block.has_value());
    block = *block_res;
    REQUIRE(block->type == TLVBlockType::Event);
    REQUIRE(block->data.size() == 16384);
    REQUIRE(std::count(block->data.begin(), block->data.end(), 'd') == 16384);

    // Then the buffer should not have been reallocated due to the tiny difference
    // in size (another implementation detail)
    REQUIRE(buffer.capacity() == capacity_c);

    // And one final read should hit EOF
    block_res = reader.ReadNext();
    REQUIRE(block_res.has_value());
    REQUIRE(*block_res == std::nullopt);

    fs.Close(handle);
  }
}
