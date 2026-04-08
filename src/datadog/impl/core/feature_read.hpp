// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/core/tlv.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog::impl {

/**
 * Lightweight wrapper for a block of TLV-formatted data read from a file. The data
 * pointed to by `data` will be an exact copy of an `event` or `event_metadata` value
 * previously enqueued for storage.
 */
struct TLVBlock {
  TLVBlockType type;
  Block data;
};

/**
 * Interface passed to Feature::UploadThread_PrepareReport, allowing it iteratively read
 * blocks of TLV-formatted data from the relevant batch file.
 */
class BatchReader {
 private:
  DiagnosticLogger& _logger;
  File _file;
  std::vector<char>& _block_data_buffer;

 public:
  /**
   * Initializes a new BatchReader to read TLV data from a file that's open for read,
   * using the provided buffer to store each block as it's read.
   */
  explicit BatchReader(
      DiagnosticLogger& in_logger, File&& in_file, std::vector<char>& in_buffer
  );

  /**
   * Result of an attempt to read a single TLV block from the file.
   */
  struct Result {
    enum class Status : uint8_t {
      Success,    // A complete TLV block was successfully read from the file
      EndOfFile,  // We're done with the file; no more blocks are present
      Error       // Failed to read or parse a block; details were logged
    } status{Status::Success};

    TLVBlock block;  // Valid only on Success
  };

  /**
   * Attempts to read the next block of TLV data from the open file.
   */
  Result ReadNext();
};

}  // namespace datadog::impl
