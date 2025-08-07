#pragma once

#include <cinttypes>
#include <optional>

#include "core/block.hpp"
#include "platform/filesystem.hpp"

namespace datadog::impl {

enum class TLVBlockType : uint16_t // NOLINT(performance-enum-size)
{
    Event = 0x0000,
    Metadata = 0x0001,
};

struct TLVBlockHeader
{
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

platform::FilesystemResult<bool> ReadTLVBlock(
    platform::IFileReader& file,
    TLVBlockType& out_type,
    std::vector<char>& out_block_data
);

}
