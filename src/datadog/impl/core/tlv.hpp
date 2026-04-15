// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <optional>

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog::impl {

enum class TLVBlockType : uint16_t  // NOLINT(performance-enum-size)
{
  Event = 0x0000,
  Metadata = 0x0001,
};

struct TLVBlockHeader {
  static const size_t SIZE = 6;
  static_assert(
      SIZE == sizeof(TLVBlockType) + sizeof(uint32_t),
      "Unexpected binary size for unpacked TLV block header"
  );

  TLVBlockType type;
  uint32_t block_size;

  static std::optional<TLVBlockHeader> Decode(const char buf[SIZE]);
  void Encode(char buf[SIZE]) const;
};

/**
 * Encodes a block of binary data to the given buffer, with the accompanying header
 * prepended.
 *
 * @param dst Pointer to a contiguous region of memory that can fit the block, header
 *  included.
 * @param n Number of available bytes in that buffer. Must be greater than or equal to
 *  block.size() + TLVBlockHeader::SIZE;
 * @param type The tag used to identify the type of this block.
 * @param data The data that constitutes the value of the block.
 */
size_t EncodeTLVBlock(char* dst, size_t n, TLVBlockType type, Block data);

/**
 * Result of an attempt to read a TLV block header from a file.
 */
struct TLVBlockHeaderReadResult {
  enum class Status : uint8_t {
    Success,    // Read OK: header is populated
    EndOfFile,  // Read OK, but there are no more blocks in the file: we're at EOF
    Error       // Read failed or data malformed; error was logged
  } status{Status::Success};

  TLVBlockHeader header{TLVBlockType::Event, 0};  // Valid only on Success
};

/**
 * Attempts to read the header for the next block of TLV-formatted data from an open
 * file.
 *
 * If result.status is Success, result.header is populated with a valid TLV header
 * describing the block of data that's now at the file handle's current read offset.
 *
 * If result.status is EndOfFile, the file handle is now positioned at the end of the
 * file, having cleanly read all available blocks.
 *
 * If result.status is Error, the file read failed, not enough data was available to
 * read a complete header, or the header itself was malformed. In the event of an error,
 * details will be logged as a diagnostic warning.
 */
TLVBlockHeaderReadResult ReadTLVBlockHeader(DiagnosticLogger& logger, File& file);

/**
 * Attempts to read the contents of a TLV block (i.e. the 'V' portion following the 'TL'
 * header) from an open file, given the size encoded in the preceding block header.
 *
 * If successful, populates the given reusable buffer with the full contents of the
 * block and returns true. On read failure or incomplete read, logs a diagnostic warning
 * and returns false.
 */
bool ReadTLVBlockData(
    DiagnosticLogger& logger, File& file, size_t size, std::vector<char>& out_value
);

}  // namespace datadog::impl
