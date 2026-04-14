// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/feature_read.hpp"

#include "datadog/impl/assert.hpp"

namespace datadog::impl {

BatchReader::BatchReader(
    DiagnosticLogger& in_logger, File&& in_file, std::vector<char>& in_buffer
)
    : _logger(in_logger), _file(std::move(in_file)), _block_data_buffer(in_buffer) {}

BatchReader::Result BatchReader::ReadNext() {
  // Read and parse the TLV header at the current file position
  const TLVBlockHeaderReadResult res = ReadTLVBlockHeader(_logger, _file);
  switch (res.status) {
    // Successful read; continue
    case TLVBlockHeaderReadResult::Status::Success:
      break;

    // Read OK but there are no more blocks
    case TLVBlockHeaderReadResult::Status::EndOfFile:
      return Result{Result::Status::EndOfFile, {}};

    // Read failed; return error
    case TLVBlockHeaderReadResult::Status::Error:
      return Result{Result::Status::Error, {}};
  }

  // Header read OK; read the next N bytes to get the data for this block, as advertised
  // in the header
  const bool ok =
      ReadTLVBlockData(_logger, _file, res.header.block_size, _block_data_buffer);
  if (!ok) {
    return Result{Result::Status::Error, {}};
  }

  // Successful read; block is valid: construct a lightweight view of our member
  // vector, and return a result with a TLVBlock object
  Block block_data{_block_data_buffer.data(), _block_data_buffer.size()};
  DATADOG_ASSERT(
      block_data.size() == res.header.block_size,
      "After OK ReadTLVBlock, buffer size does not match block size in header"
  );
  return Result{Result::Status::Success, TLVBlock{res.header.type, block_data}};
}

}  // namespace datadog::impl
