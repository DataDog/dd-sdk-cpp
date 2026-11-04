// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tlv.hpp"

#include <array>
#include <cstring>

#include "datadog/impl/core/storage/util.hpp"
#include "datadog/impl/types/assert.hpp"
#include "datadog/impl/types/endian.hpp"

namespace datadog::impl {

std::optional<TLVBlockHeader> TLVBlockHeader::Decode(
    const char buf[TLVBlockHeader::SIZE]
) {
  // Multi-byte binary values are always written big-endian; decode them
  uint16_t type = ReadBigEndian16(buf);
  uint32_t block_size = ReadBigEndian32(buf + sizeof(uint16_t));

  // Zero-length blocks are explicitly disallowed
  if (block_size == 0) {
    return std::nullopt;
  }

  // Parse block type
  switch (type) {
    // If the value is recognized, we're good
    case static_cast<uint16_t>(TLVBlockType::Event):
    case static_cast<uint16_t>(TLVBlockType::Metadata):
      return TLVBlockHeader{static_cast<TLVBlockType>(type), block_size};

    // For all other values, break and return nullopt
    default:
      break;
  }
  return std::nullopt;
}

void TLVBlockHeader::Encode(char buf[TLVBlockHeader::SIZE]) const {
  WriteBigEndian16(buf, static_cast<uint16_t>(type));
  WriteBigEndian32(buf + sizeof(uint16_t), block_size);
}

size_t EncodeTLVBlock(char* dst, size_t n, TLVBlockType type, Block data) {
  // We should not attempt to write empty blocks
  DATADOG_ASSERT(n > 0, "EncodeTLVBlock called with size 0");

  // Construct the header and validate that the buffer is large enough
  const TLVBlockHeader header{type, static_cast<uint32_t>(data.size())};
  const size_t num_bytes = TLVBlockHeader::SIZE + header.block_size;
  if (num_bytes > n) {
    return 0;
  }

  // Write the header into the start of the buffer
  header.Encode(dst);

  // Copy the entire block to the region just after the header
  std::memcpy(dst + TLVBlockHeader::SIZE, data.data(), data.size());
  return num_bytes;
}

TLVBlockHeaderReadResult ReadTLVBlockHeader(DiagnosticLogger& logger, File& file) {
  // Attempt to read the next six bytes from the file, failing with a warning if the
  // read fails or if there isn't enough data available
  std::array<char, TLVBlockHeader::SIZE> buf{};
  const IFilesystem::ReadResult res = file.Read(buf.data(), buf.size());

  // If we've reached the end of the file cleanly, there are no more blocks to read, and
  // we're done with the file: signal EOF
  const bool eof = res.value == FilesystemResult::OK && res.bytes_read == 0;
  if (eof) {
    return TLVBlockHeaderReadResult{TLVBlockHeaderReadResult::Status::EndOfFile};
  }

  // If we encountered a filesystem error, or if we read an incomplete header (more than
  // 0 bytes but less than the header size), log a warning and signal failure
  if (res.value != FilesystemResult::OK || res.bytes_read < TLVBlockHeader::SIZE) {
    logger.Warning(
        "Failed to read TLV block header from file",
        {{"error", FilesystemResultStr(res.value)}, {"num_bytes_read", res.bytes_read}}
    );
    return TLVBlockHeaderReadResult{TLVBlockHeaderReadResult::Status::Error};
  }

  // Parse the header, failing with a warning if it doesn't have a valid TLV Block type
  // value or the expected format
  auto header = TLVBlockHeader::Decode(buf.data());
  if (!header) {
    logger.Warning("Failed to parse TLV block header from file: invalid format");
    return TLVBlockHeaderReadResult{TLVBlockHeaderReadResult::Status::Error};
  }

  // We've successfully read a valid TLV block header; return it
  return TLVBlockHeaderReadResult{TLVBlockHeaderReadResult::Status::Success, *header};
}

bool ReadTLVBlockData(
    DiagnosticLogger& logger, File& file, size_t size, std::vector<char>& out_value
) {
  // Ensure we can fit the block data in our reusable buffer, reallocating as needed
  out_value.reserve(QuantizeBufferSize(size));
  out_value.resize(size);

  // Read the next N bytes, where N is the block size indicated in the header
  const auto res = file.Read(out_value.data(), out_value.size());
  if (res.value != FilesystemResult::OK || res.bytes_read != size) {
    logger.Warning(
        "Failed to read TLV block data from file",
        {{"error", FilesystemResultStr(res.value)}, {"num_bytes_read", res.bytes_read}}
    );
    return false;
  }
  return true;
}

}  // namespace datadog::impl
