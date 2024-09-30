// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/storage/tlv_file_reader.h"

#include "mock_datadog_file_system.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)

namespace {

using namespace std::literals::string_view_literals;

using datadog::core::storage::DatadogFileStatus;
using datadog::core::storage::StorageBlockType;
using datadog::core::storage::TLVBlock;
using datadog::core::storage::TLVFileReader;
using datadog::core::storage::mocks::MockDatadogFile;
using trompeloeil::_;

TEST_CASE("M read block data W ReadBlock", "[feature_storage]") {
  // Given
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  auto contents = "File contents"sv;

  // Expect
  trompeloeil::sequence seq;
  REQUIRE_CALL(*mock_file, Read(_, _))
      .WITH(_2 == sizeof(StorageBlockType))
      .SIDE_EFFECT({
        *reinterpret_cast<StorageBlockType*>(_1) = StorageBlockType::Event;
        _2 = sizeof(StorageBlockType);
      })
      .IN_SEQUENCE(seq)
      .RETURN(true);
  REQUIRE_CALL(*mock_file, Read(_, _))
      .WITH(_2 == sizeof(uint32_t))
      .SIDE_EFFECT({
        *reinterpret_cast<uint32_t*>(_1) = contents.size();
        _2 = sizeof(uint32_t);
      })
      .IN_SEQUENCE(seq)
      .RETURN(true);
  REQUIRE_CALL(*mock_file, Read(_, _))
      .WITH(_2 == contents.size())
      .SIDE_EFFECT({
        memcpy(_1, contents.data(), contents.size());
        _2 = contents.size();
      })
      .IN_SEQUENCE(seq)
      .RETURN(true);

  // When
  TLVFileReader reader{std::move(mock_file)};
  TLVBlock block;
  REQUIRE(DatadogFileStatus::Ok == reader.ReadBlock(block));

  // Then
  REQUIRE(block.block_type == StorageBlockType::Event);
  REQUIRE(block.data.size() == contents.size());
  std::string read{block.data.begin(), block.data.end()};
  REQUIRE(read == contents);
}

TEST_CASE("M return status W ReadBlock { failure on type read }",
          "[feature_storage]") {
  // Given
  auto mock_file = std::make_unique<MockDatadogFile>("any");

  // Expect
  REQUIRE_CALL(*mock_file, Read(_, _))
      .WITH(_2 == sizeof(StorageBlockType))
      .SIDE_EFFECT({
        *reinterpret_cast<StorageBlockType*>(_1) = StorageBlockType::Event;
        _2 = sizeof(StorageBlockType);
      })
      .RETURN(false);
  // Before we move the file, set up its bad state
  mock_file->SetStatus(DatadogFileStatus::BadState);

  // When
  TLVFileReader reader{std::move(mock_file)};
  TLVBlock block;
  DatadogFileStatus result = reader.ReadBlock(block);

  // Then
  REQUIRE(DatadogFileStatus::BadState == result);
}

TEST_CASE("M return status W ReadBlock { failure on size read }",
          "[feature_storage]") {
  // Given
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  auto contents = "File contents"sv;

  // Expect
  REQUIRE_CALL(*mock_file, Read(_, _))
      .WITH(_2 == sizeof(StorageBlockType))
      .SIDE_EFFECT({
        *reinterpret_cast<StorageBlockType*>(_1) = StorageBlockType::Event;
        _2 = sizeof(StorageBlockType);
      })
      .RETURN(true);
  REQUIRE_CALL(*mock_file, Read(_, _))
      .WITH(_2 == sizeof(uint32_t))
      .SIDE_EFFECT({
        *reinterpret_cast<uint32_t*>(_1) = contents.size();
        _2 = sizeof(uint32_t);
      })
      .RETURN(false);
  // Before we move the file, set up its bad state
  mock_file->SetStatus(DatadogFileStatus::BadState);

  // When
  TLVFileReader reader{std::move(mock_file)};
  TLVBlock block;
  DatadogFileStatus result = reader.ReadBlock(block);

  // Then
  REQUIRE(DatadogFileStatus::BadState == result);
}

TEST_CASE("M return status W ReadBlock { failure on data read }",
          "[feature_storage]") {
  // Given
  auto mock_file = std::make_unique<MockDatadogFile>("any");
  auto contents = "File contents"sv;

  // Expect
  REQUIRE_CALL(*mock_file, Read(_, _))
      .WITH(_2 == sizeof(StorageBlockType))
      .SIDE_EFFECT({
        *reinterpret_cast<StorageBlockType*>(_1) = StorageBlockType::Event;
        _2 = sizeof(StorageBlockType);
      })
      .RETURN(true);
  REQUIRE_CALL(*mock_file, Read(_, _))
      .WITH(_2 == sizeof(uint32_t))
      .SIDE_EFFECT({
        *reinterpret_cast<uint32_t*>(_1) = contents.size();
        _2 = sizeof(uint32_t);
      })
      .RETURN(true);
  REQUIRE_CALL(*mock_file, Read(_, _))
      .WITH(_2 == contents.size())
      .RETURN(false);
  // Before we move the file, set up its bad state
  mock_file->SetStatus(DatadogFileStatus::BadState);

  // When
  TLVFileReader reader{std::move(mock_file)};
  TLVBlock block;
  DatadogFileStatus result = reader.ReadBlock(block);

  // Then
  REQUIRE(DatadogFileStatus::BadState == result);
}

}  // namespace

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
