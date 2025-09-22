// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <optional>

#include "assert.hpp"
#include "core/block.hpp"
#include "core/tlv.hpp"
#include "nonstd/expected.hpp"
#include "platform/filesystem.hpp"

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
 * Reason that we failed to read the next TLVBlock from a batch file.
 */
enum class BatchReadError : uint8_t {
  /**
   * File reads were OK, but the data is malformed: e.g. TLV block type was
   * unrecognized, TLV header showed a block size of 0, etc.
   */
  InvalidBlockFormat,
  /**
   * An attempt to read from the file was unsuccessful, e.g. because the file no longer
   * exists, our handle is invalid, etc.
   */
  FailedRead,
  /**
   * An attempt to read from the file failed due to a catastrophic filesystem error
   * (i.e. the bad bit was set).
   */
  IOError
};

/**
 * Interface passed to Feature::UploadThread_PrepareReport, allowing it iteratively read
 * blocks of TLV-formatted data from the relevant batch file.
 */
class BatchReader {
 private:
  platform::IFileReader& _file;
  std::vector<char>& _block_data_buffer;

 public:
  /**
   * Initializes a new BatchReader to read TLV data from a file that's open for read,
   * using the provided buffer to store each block as it's read.
   */
  BatchReader(platform::IFileReader& file, std::vector<char>& buffer);

  /**
   * Attempts to read the next block of TLV data from the open file, returning
   * std::nullopt if it's reached the end of the file and no more blocks are available.
   * Returns an error value if the file can not be read or the data is malformed.
   */
  nonstd::expected<std::optional<TLVBlock>, BatchReadError> ReadNext();
};

}  // namespace datadog::impl
