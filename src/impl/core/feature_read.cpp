// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/feature_read.hpp"

#include "assert.hpp"

namespace datadog::impl {

BatchReader::BatchReader(platform::IFileReader& file, std::vector<char>& buffer)
    : _file(file), _block_data_buffer(buffer) {}

nonstd::expected<std::optional<TLVBlock>, BatchReadError> BatchReader::ReadNext() {
  // Read and parse the TLV header at the current file position, then read the
  // adjacent block data into the buffer, returning a result that contains a
  // lightweight view of that buffer
  const TLVBlockReadResult result = ReadTLVBlock(_file, _block_data_buffer);
  switch (result.type) {
    // Successful read; continue
    case TLVBlockReadResultType::Success:
      break;

    // Read OK but there are no more blocks; return nullopt
    case TLVBlockReadResultType::EndOfFile:
      return std::nullopt;

    // On any failure, early-out with an appropriate error
    case TLVBlockReadResultType::IOError:
      return nonstd::make_unexpected(BatchReadError::IOError);
    case TLVBlockReadResultType::ReadFailed:
      return nonstd::make_unexpected(BatchReadError::FailedRead);
    case TLVBlockReadResultType::Malformed:
      return nonstd::make_unexpected(BatchReadError::InvalidBlockFormat);
  }

  // Successful read; block is valid: construct a lightweight view of our member
  // vector, and return a TLVBlock object
  Block block_data{_block_data_buffer.data(), _block_data_buffer.size()};
  DATADOG_ASSERT(
      block_data.size() == result.header.block_size,
      "After OK ReadTLVBlock, buffer size does not match block size in header"
  );
  return TLVBlock{result.header.type, block_data};
}

}  // namespace datadog::impl
