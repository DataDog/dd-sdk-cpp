#pragma once

#include <array>
#include <cinttypes>
#include <optional>

#include "core/block.hpp"
#include "platform/filesystem.hpp"

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

enum class TLVBlockReadResultType : uint8_t {
  /** A valid TLV-formatted block was read successfully. */
  Success,
  /** A low-level I/O error occurred. */
  IOError,
  /** The filesystem read operation failed. */
  ReadFailed,
  /** The data read from the file was not a valid TLV block. */
  Malformed
};

struct TLVBlockReadResult {
  /**
   * Result of the read operation.
   */
  TLVBlockReadResultType type{TLVBlockReadResultType::Malformed};
  /**
   * Block header read from file, usable only if result type is success.
   */
  TLVBlockHeader header{TLVBlockType::Event, 0};
  /**
   * If true, the read operation reached or surpassed the end of the file, and no more
   * blocks should be read.
   */
  bool eof{false};

  explicit TLVBlockReadResult(TLVBlockReadResultType in_type) : type(in_type) {}

  explicit TLVBlockReadResult(const TLVBlockHeader& in_header, bool in_eof)
      : type(TLVBlockReadResultType::Success), header(in_header), eof(in_eof) {}
};

/**
 * Reads the next block of TLV-formatted data from the open file.
 *
 * @param file The open input file to read from.
 * @param out_block_data A mutable reference to the vector where the block data (just
 *  the 'V' portion of the TLV block, not including the 'TL' header) will be written. If
 *  successful, out_block_data is guaranteed to be the exact size indicated by the
 *  length encoded in the block header.
 *
 * @returns a struct indicating the result of the operation: if result.type is Success,
 *  other values may be read. If result.eof is set, no further reads should be
 *  attempted.
 */
TLVBlockReadResult ReadTLVBlock(
    platform::IFileReader& file, std::vector<char>& out_block_data
);

}  // namespace datadog::impl
