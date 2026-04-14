// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tlv.hpp"

#include <catch2/catch_test_macros.hpp>

#include "mock/filesystem_new.hpp"
#include "mock/tlv.hpp"
#include "support/diagnostics.hpp"

using namespace datadog::impl;

TEST_CASE("TLVBlockHeader", "[unit][tlv]") {
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

TEST_CASE("EncodeTLVBlock", "[unit][tlv]") {
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

TEST_CASE("TLV Read", "[unit][tlv]") {
  // Given a file 'good' containing a metadata block and an event block
  MockFilesystemNew fs;
  MockTLVFile().AppendMetadata("metadata-0").AppendEvent("event-0").WriteTo(fs, "good");
  auto good_res = fs.Wrapper().OpenForRead("good", false);
  REQUIRE(good_res.value == FilesystemResult::OK);
  File& good = good_res.file;

  // And another file 'bad' containing 6 bytes of garbage data
  fs.Touch("bad", "666666");
  auto bad_res = fs.Wrapper().OpenForRead("bad", false);
  REQUIRE(bad_res.value == FilesystemResult::OK);
  File& bad = bad_res.file;

  // And a diagnostic logger that will buffer all messages emitted
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  // And a helper function that we can use to seek past one block at a time
  auto skip_block = [&logger](File& file) {
    const auto res = ReadTLVBlockHeader(logger, file);
    REQUIRE(res.status == TLVBlockHeaderReadResult::Status::Success);
    std::vector<char> ignored;
    const bool ok = ReadTLVBlockData(logger, file, res.header.block_size, ignored);
    REQUIRE(ok);
  };

  SECTION("ReadTLVBlockHeader") {
    SECTION("M parse header W handle is positioned at valid TLV metadata header") {
      // When we attempt to read a TLV header from the start of our good file
      const TLVBlockHeaderReadResult res = ReadTLVBlockHeader(logger, good);

      // Then we successfully parse the header for the initial metadata block
      REQUIRE(res.status == TLVBlockHeaderReadResult::Status::Success);
      REQUIRE(res.header.type == TLVBlockType::Metadata);
      REQUIRE(res.header.block_size == std::string_view{"metadata-0"}.size());

      // And no diagnostic warnings or errors are emitted
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M parse header W handle is positioned at valid TLV event header") {
      // Given a file handle that's positioned at the second block in our good file
      skip_block(good);

      // When we attempt to read a TLV header from that position
      const TLVBlockHeaderReadResult res = ReadTLVBlockHeader(logger, good);

      // Then we successfully parse the header for the event block
      REQUIRE(res.status == TLVBlockHeaderReadResult::Status::Success);
      REQUIRE(res.header.type == TLVBlockType::Event);
      REQUIRE(res.header.block_size == std::string_view{"event-0"}.size());

      // And no diagnostic warnings or errors are emitted
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M cleanly return EOF W handle is positioned at end of file") {
      // Given a file handle that's positioned at the end of our good file
      skip_block(good);
      skip_block(good);

      // When we attempt to read a TLV header from that position
      const TLVBlockHeaderReadResult res = ReadTLVBlockHeader(logger, good);

      // Then we get a result indicating there are no more blocks in the file
      REQUIRE(res.status == TLVBlockHeaderReadResult::Status::EndOfFile);

      // And no diagnostic warnings or errors are emitted
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M fail and log a warning W file does not contain a valid TLV header") {
      // When we attempt to read a TLV header from our bad file
      const TLVBlockHeaderReadResult res = ReadTLVBlockHeader(logger, bad);

      // Then we get a result indicating that we failed to parse a TLV header
      REQUIRE(res.status == TLVBlockHeaderReadResult::Status::Error);

      // And a warning is emitted
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 1);
      REQUIRE(
          diagnostics.warning[0] ==
          "Failed to parse TLV block header from file: invalid format"
      );
    }

    SECTION("M fail and log a warning W unable to read a complete header") {
      // Given a handle into our bad file that's positioned one byte in, meaning there
      // are only 5 trailing bytes, not enough for a complete TLV header
      char c;
      const auto read_res = bad.Read(&c, 1);
      REQUIRE(read_res.value == FilesystemResult::OK);
      REQUIRE(read_res.bytes_read == 1);

      // When we attempt to read a TLV header
      const TLVBlockHeaderReadResult res = ReadTLVBlockHeader(logger, bad);

      // Then we get a result indicating that we failed to parse a TLV header
      REQUIRE(res.status == TLVBlockHeaderReadResult::Status::Error);

      // And a warning is emitted that indicates a partial read
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 1);
      REQUIRE(
          diagnostics.warning[0] ==
          "Failed to read TLV block header from file {\"error\":\"OK\","
          "\"num_bytes_read\":5}"
      );
    }

    SECTION("M fail and log a warning W file read operation fails") {
      // Given a filesystem that will refuse to allow our good file to be read
      fs.SimulateFailure(
          "good", FilesystemResult::UnknownError, MockFilesystemNew::FailureFlags::IO
      );

      // When we attempt to read a TLV header from our good file
      const TLVBlockHeaderReadResult res = ReadTLVBlockHeader(logger, good);

      // Then we get a result indicating that we failed to parse a TLV header
      REQUIRE(res.status == TLVBlockHeaderReadResult::Status::Error);

      // And a warning is emitted that shows the filesystem error
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 1);
      REQUIRE(
          diagnostics.warning[0] ==
          "Failed to read TLV block header from file {\"error\":\"UnknownError\","
          "\"num_bytes_read\":0}"
      );
    }
  }

  SECTION("ReadTLVBlockData") {
    SECTION("M successfully read block W positioned after TLV header") {
      // Given a file handle positioned at the first metadata block in our good file
      const auto res = ReadTLVBlockHeader(logger, good);
      REQUIRE(res.status == TLVBlockHeaderReadResult::Status::Success);

      // When we attempt to read the block data positioned after the header
      std::vector<char> got;
      const bool ok = ReadTLVBlockData(logger, good, res.header.block_size, got);

      // Then the read is successful
      REQUIRE(ok);

      // And our vector is populated with the binary value from the block
      std::string_view got_str(got.data(), got.size());
      REQUIRE(got_str == "metadata-0");

      // And no diagnostic warnings or errors are emitted
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 0);
    }

    SECTION("M fail and log a warning W file ends before advertised block size") {
      // When we attempt to read a supposed 100-byte value from a file that only has 6
      // bytes left to read
      std::vector<char> got;
      const bool ok = ReadTLVBlockData(logger, bad, 100, got);

      // Then the read is unsuccessful
      REQUIRE(!ok);

      // And a warning is emitted that shows we found insufficient data
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 1);
      REQUIRE(
          diagnostics.warning[0] ==
          "Failed to read TLV block data from file {\"error\":\"OK\","
          "\"num_bytes_read\":6}"
      );
    }

    SECTION("M fail and log a warning W file read operation fails") {
      // Given a file handle positioned at the first metadata block in our good file
      const auto res = ReadTLVBlockHeader(logger, good);
      REQUIRE(res.status == TLVBlockHeaderReadResult::Status::Success);

      // And a filesystem that will now refuse to allow our good file to be read
      fs.SimulateFailure(
          "good", FilesystemResult::UnknownError, MockFilesystemNew::FailureFlags::IO
      );

      // When we attempt to read the contents of that TLV metadata block
      std::vector<char> got;
      const bool ok = ReadTLVBlockData(logger, good, res.header.block_size, got);

      // Then the read is unsuccessful
      REQUIRE(!ok);

      // And a warning is emitted that shows the filesystem error
      REQUIRE(diagnostics.error.size() == 0);
      REQUIRE(diagnostics.warning.size() == 1);
      REQUIRE(
          diagnostics.warning[0] ==
          "Failed to read TLV block data from file {\"error\":\"UnknownError\","
          "\"num_bytes_read\":0}"
      );
    }
  }
}
