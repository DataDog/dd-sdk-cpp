// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tlv.hpp"

#include <catch2/catch_test_macros.hpp>

#include "datadog/impl/storage/path.hpp"

#include "mock/filesystem_new.hpp"
#include "mock/tlv.hpp"

using namespace datadog::impl;

TEST_CASE("TLVBlockHeader", "[unit]") {
  SECTION("M be serialized properly W Encode is called") {
    // Given a six-byte buffer
    char buf[6];

    // When we encode a header
    uint32_t size = 131198;  // 0x2007e
    TLVBlockHeader{TLVBlockType::Metadata, size}.Encode(buf);

    // Then the block type is encoded in the first two bytes, big-endian
    REQUIRE(buf[0] == 0x00);
    REQUIRE(buf[1] == 0x01);

    // And the size is encoded in the next four bytes, big-endian
    REQUIRE(buf[2] == 0x00);
    REQUIRE(buf[3] == 0x02);
    REQUIRE(buf[4] == 0x00);
    REQUIRE(buf[5] == 0x7e);
  }

  SECTION("M be deserialized properly W Decode is called") {
    // Given a six-byte buffer with a uint16 type of Metadata (1) and uint32 size
    // 0x2007e, big-endian
    char buf[6] = {0x00, 0x01, 0x00, 0x02, 0x00, 0x7e};

    // When we decode those bytes as a header
    auto header = TLVBlockHeader::Decode(buf);

    // Then we should have a valid header
    REQUIRE(header.has_value());

    // And the values in that header should match what we expect
    REQUIRE(header->type == TLVBlockType::Metadata);
    REQUIRE(header->block_size == 131198);
  }
}

TEST_CASE("EncodeTLVBlock", "[unit]") {
  SECTION("M encode Event block properly W valid buffer and data provided") {
    // Given a buffer large enough for header + data
    char buf[20];
    const std::string test_data = "hello";
    Block block_data{test_data};

    // When we encode an Event TLV block
    size_t bytes_written =
        EncodeTLVBlock(buf, sizeof(buf), TLVBlockType::Event, block_data);

    // Then the correct number of bytes should be written
    REQUIRE(bytes_written == TLVBlockHeader::SIZE + test_data.size());
    REQUIRE(bytes_written == 11);  // 6 byte header + 5 byte payload

    // And the header should contain Event type (0x0000)
    REQUIRE(buf[0] == 0x00);
    REQUIRE(buf[1] == 0x00);

    // And the size should be encoded as 5 (big-endian)
    REQUIRE(buf[2] == 0x00);
    REQUIRE(buf[3] == 0x00);
    REQUIRE(buf[4] == 0x00);
    REQUIRE(buf[5] == 0x05);

    // And the data should follow the header
    REQUIRE(
        std::string_view(buf + TLVBlockHeader::SIZE, test_data.size()) == test_data
    );
  }

  SECTION("M encode Metadata block properly W valid buffer and data provided") {
    // Given a buffer and some metadata
    char buf[30];
    const std::string metadata = "metadata_content";
    Block block_data{metadata};

    // When we encode a Metadata TLV block
    size_t bytes_written =
        EncodeTLVBlock(buf, sizeof(buf), TLVBlockType::Metadata, block_data);

    // Then the correct number of bytes should be written
    REQUIRE(bytes_written == TLVBlockHeader::SIZE + metadata.size());

    // And the header should contain Metadata type (0x0001)
    REQUIRE(buf[0] == 0x00);
    REQUIRE(buf[1] == 0x01);

    // And the size should be encoded properly (big-endian)
    REQUIRE(buf[2] == 0x00);
    REQUIRE(buf[3] == 0x00);
    REQUIRE(buf[4] == 0x00);
    REQUIRE(buf[5] == static_cast<char>(metadata.size()));

    // And the data should follow the header
    REQUIRE(std::string_view(buf + TLVBlockHeader::SIZE, metadata.size()) == metadata);
  }

  SECTION("M return 0 W buffer is too small") {
    // Given a buffer smaller than header + data
    char small_buf[5];  // Only 5 bytes, need at least 6 for header
    const std::string test_data = "test";
    Block block_data{test_data};

    // When we try to encode with insufficient buffer space
    size_t bytes_written =
        EncodeTLVBlock(small_buf, sizeof(small_buf), TLVBlockType::Event, block_data);

    // Then no bytes should be written
    REQUIRE(bytes_written == 0);
  }

  SECTION("M handle empty data properly W empty block provided") {
    // Given a buffer and empty data
    char buf[10];
    Block empty_block{""};

    // When we encode an empty TLV block
    size_t bytes_written =
        EncodeTLVBlock(buf, sizeof(buf), TLVBlockType::Event, empty_block);

    // Then only the header should be written
    REQUIRE(bytes_written == TLVBlockHeader::SIZE);

    // And the size field should be 0
    REQUIRE(buf[2] == 0x00);
    REQUIRE(buf[3] == 0x00);
    REQUIRE(buf[4] == 0x00);
    REQUIRE(buf[5] == 0x00);
  }

  SECTION("M handle large data blocks W buffer is exactly sized") {
    // Given a larger data block and exactly sized buffer
    std::string large_data(1000, 'x');
    Block block_data{large_data};
    std::vector<char> buf(TLVBlockHeader::SIZE + large_data.size());

    // When we encode the large block
    size_t bytes_written =
        EncodeTLVBlock(buf.data(), buf.size(), TLVBlockType::Metadata, block_data);

    // Then all bytes should be written correctly
    REQUIRE(bytes_written == buf.size());

    // And the size should be encoded properly (1000 = 0x03E8)
    REQUIRE(static_cast<unsigned char>(buf[2]) == 0x00);
    REQUIRE(static_cast<unsigned char>(buf[3]) == 0x00);
    REQUIRE(static_cast<unsigned char>(buf[4]) == 0x03);
    REQUIRE(static_cast<unsigned char>(buf[5]) == 0xE8);

    // And the data should be preserved
    REQUIRE(
        std::string_view(buf.data() + TLVBlockHeader::SIZE, large_data.size()) ==
        large_data
    );
  }
}

TEST_CASE("ReadTLVBlock", "[unit]") {
  SECTION("M read all blocks successfully W given valid TLV file") {
    // Given a file 'foo' containing a metadata block and an event block
    MockFilesystemNew fs;
    MockTLVFile()
        .AppendMetadata("metadata-0")
        .AppendEvent("event-0")
        .WriteTo(fs, "foo");
    impl::PlatformPath pp;
    pp.Encode("foo");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // And a reusable read buffer
    std::vector<char> buffer;

    // When we read from the file once
    auto result = ReadTLVBlock(fs, handle, buffer);

    // Then we should get a valid result with our metadata block
    REQUIRE(result.type == TLVBlockReadResultType::Success);
    REQUIRE(result.header.type == TLVBlockType::Metadata);
    REQUIRE(result.header.block_size == 10);

    // And our buffer should contain the block data
    REQUIRE(buffer.size() == result.header.block_size);
    REQUIRE(std::string_view(buffer.data(), buffer.size()) == "metadata-0");

    // Next: When we read from the file a second time
    result = ReadTLVBlock(fs, handle, buffer);

    // Then we should get the next and final block
    REQUIRE(result.type == TLVBlockReadResultType::Success);
    REQUIRE(result.header.type == TLVBlockType::Event);
    REQUIRE(result.header.block_size == 7);

    // And our buffer should be reused for its data
    REQUIRE(buffer.size() == result.header.block_size);
    REQUIRE(std::string_view(buffer.data(), buffer.size()) == "event-0");

    // And our buffer's capacity should be unchanged
    REQUIRE(buffer.capacity() >= 10);

    // Next: When we attempt to read once more
    result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);

    // Then we should get EndOfFile
    REQUIRE(result.type == TLVBlockReadResultType::EndOfFile);
  }

  SECTION("M return IOError W file read operation encounters I/O error") {
    // Given a file that suddenly becomes unreadable after being opened
    MockFilesystemNew fs;
    MockTLVFile().AppendEvent("test").WriteTo(fs, "corruptible");
    impl::PlatformPath pp;
    pp.Encode("corruptible");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);
    fs.Corrupt("corruptible");

    // When we try to read from the corrupted file
    std::vector<char> buffer;
    auto result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);

    // Then we should get an IOError result
    REQUIRE(result.type == TLVBlockReadResultType::IOError);
  }

  SECTION("M return ReadFailed W file read operation fails") {
    // Given a file we suddenly can't read from after it's open
    MockFilesystemNew fs;
    MockTLVFile().AppendEvent("test").WriteTo(fs, "failing");
    impl::PlatformPath pp;
    pp.Encode("failing");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);
    fs.SetFail("failing", true);

    // When we try to read from the failing file
    std::vector<char> buffer;
    auto result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);

    // Then we should get a ReadFailed result
    REQUIRE(result.type == TLVBlockReadResultType::ReadFailed);
  }

  SECTION("M return Malformed W file contains invalid TLV header") {
    // Given a file with invalid block type in header
    MockFilesystemNew fs;
    MockTLVFile malformed_file;
    // Manually create invalid header: type 0x9999 (unknown), size 4
    malformed_file.AppendBytes(std::string_view{"\x99\x99\x00\x00\x00\x04test", 10});
    malformed_file.WriteTo(fs, "malformed");
    impl::PlatformPath pp;
    pp.Encode("malformed");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // When we try to read the malformed header
    std::vector<char> buffer;
    auto result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);

    // Then we should get a Malformed result
    REQUIRE(result.type == TLVBlockReadResultType::Malformed);
  }

  SECTION("M return Malformed W file contains zero-size block") {
    // Given a file with zero block size in header
    MockFilesystemNew fs;
    MockTLVFile zero_size_file;
    // Create header with Event type but zero size
    zero_size_file.AppendBytes(std::string_view{"\x00\x00\x00\x00\x00\x00", 6});
    zero_size_file.WriteTo(fs, "zero_size");
    impl::PlatformPath pp;
    pp.Encode("zero_size");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // When we try to read the zero-size block header
    std::vector<char> buffer;
    auto result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);

    // Then we should get a Malformed result (zero-size blocks are disallowed)
    REQUIRE(result.type == TLVBlockReadResultType::Malformed);
  }

  SECTION("M return Malformed W file has incomplete header") {
    // Given a file with only partial header (less than 6 bytes)
    MockFilesystemNew fs;
    fs.Touch("partial_header", std::string_view{"\x00\x01\x00", 3});
    impl::PlatformPath pp;
    pp.Encode("partial_header");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // When we try to read the incomplete header
    std::vector<char> buffer;
    auto result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);

    // Then we should get a Malformed result (not a valid TLV header)
    REQUIRE(result.type == TLVBlockReadResultType::Malformed);
  }

  SECTION("M return Malformed W file has incomplete block data") {
    // Given a file with valid header but insufficient block data
    MockFilesystemNew fs;
    MockTLVFile incomplete_file;
    // Header says size is 10 bytes, but we only provide 5
    incomplete_file.AppendBytes(std::string_view{"\x00\x01\x00\x00\x00\x0a", 6});
    incomplete_file.AppendBytes("12345");
    incomplete_file.WriteTo(fs, "incomplete");
    impl::PlatformPath pp;
    pp.Encode("incomplete");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // When we try to read the block with insufficient data
    std::vector<char> buffer;
    auto result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);

    // Then we should get a Malformed result
    REQUIRE(result.type == TLVBlockReadResultType::Malformed);
  }

  SECTION("M return EndOfFile W file is empty") {
    // Given an empty file
    MockFilesystemNew fs;
    fs.Touch("empty", "");
    impl::PlatformPath pp;
    pp.Encode("empty");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // When we try to read from the empty file
    std::vector<char> buffer;
    auto result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);

    // Then we should get EndOfFile
    REQUIRE(result.type == TLVBlockReadResultType::EndOfFile);
  }

  SECTION("M handle large block size properly W buffer needs reallocation") {
    // Given a file with a large block
    MockFilesystemNew fs;
    std::string large_data(5000, 'L');  // 5KB of data
    MockTLVFile().AppendMetadata(large_data).WriteTo(fs, "large");
    impl::PlatformPath pp;
    pp.Encode("large");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    // And a buffer that starts small
    std::vector<char> buffer;
    buffer.reserve(100);  // Start with small capacity

    // When we read the large block
    auto result = ReadTLVBlock(fs, handle, buffer);

    // Then we should get success
    REQUIRE(result.type == TLVBlockReadResultType::Success);
    REQUIRE(result.header.type == TLVBlockType::Metadata);
    REQUIRE(result.header.block_size == 5000);

    // And the buffer should have been reallocated to fit the data
    REQUIRE(buffer.size() == 5000);
    REQUIRE(buffer.capacity() >= 5000);

    // And the data should be correct
    REQUIRE(std::string_view(buffer.data(), buffer.size()) == large_data);

    // And a subsequent read should return EOF
    result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);
    REQUIRE(result.type == TLVBlockReadResultType::EndOfFile);
  }

  SECTION("M reuse buffer efficiently W multiple reads of different sizes") {
    // Given a file with blocks of different sizes
    MockFilesystemNew fs;
    MockTLVFile()
        .AppendEvent("small")                   // 5 bytes
        .AppendMetadata("much_larger_content")  // 19 bytes
        .AppendEvent("med")                     // 3 bytes
        .WriteTo(fs, "multi_size");
    impl::PlatformPath pp;
    pp.Encode("multi_size");
    auto [open_result, handle] = fs.OpenForRead(pp, false);
    REQUIRE(open_result == impl::FilesystemResult::OK);

    std::vector<char> buffer;

    // When we read the first (small) block
    auto result = ReadTLVBlock(fs, handle, buffer);

    // Then we should get the small block
    REQUIRE(result.type == TLVBlockReadResultType::Success);
    REQUIRE(result.header.type == TLVBlockType::Event);
    REQUIRE(result.header.block_size == 5);
    REQUIRE(buffer.size() == 5);
    REQUIRE(std::string_view(buffer.data(), buffer.size()) == "small");
    size_t capacity_after_first = buffer.capacity();

    // When we read the second (large) block
    result = ReadTLVBlock(fs, handle, buffer);

    // Then we should get the large block with increased capacity
    REQUIRE(result.type == TLVBlockReadResultType::Success);
    REQUIRE(result.header.type == TLVBlockType::Metadata);
    REQUIRE(result.header.block_size == 19);
    REQUIRE(buffer.size() == 19);
    REQUIRE(std::string_view(buffer.data(), buffer.size()) == "much_larger_content");
    REQUIRE(buffer.capacity() >= capacity_after_first);  // Should have grown

    // When we read the third (small again) block
    result = ReadTLVBlock(fs, handle, buffer);

    // Then we should reuse the existing buffer capacity
    REQUIRE(result.type == TLVBlockReadResultType::Success);
    REQUIRE(result.header.type == TLVBlockType::Event);
    REQUIRE(result.header.block_size == 3);
    REQUIRE(buffer.size() == 3);
    REQUIRE(std::string_view(buffer.data(), buffer.size()) == "med");
    // Capacity should be unchanged since buffer was large enough
    REQUIRE(buffer.capacity() >= 19);

    // And a subsequent read should return EOF
    result = ReadTLVBlock(fs, handle, buffer);
    fs.Close(handle);
    REQUIRE(result.type == TLVBlockReadResultType::EndOfFile);
  }
}
