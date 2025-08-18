#include "core/tlv.hpp"

namespace datadog::impl {

static uint16_t _read_big_endian_16(const char* src)
{
    const unsigned char c0 = static_cast<unsigned char>(src[0]);
    const unsigned char c1 = static_cast<unsigned char>(src[1]);
    const uint16_t b0 = static_cast<uint16_t>(c0) << 8;
    const uint16_t b1 = static_cast<uint16_t>(c1) << 0;
    return b0 | b1;
}

static uint32_t _read_big_endian_32(const char* src)
{
    const unsigned char c0 = static_cast<unsigned char>(src[0]);
    const unsigned char c1 = static_cast<unsigned char>(src[1]);
    const unsigned char c2 = static_cast<unsigned char>(src[2]);
    const unsigned char c3 = static_cast<unsigned char>(src[3]);
    const uint32_t b0 = static_cast<uint32_t>(c0) << 24;
    const uint32_t b1 = static_cast<uint32_t>(c1) << 16;
    const uint32_t b2 = static_cast<uint32_t>(c2) << 8;
    const uint32_t b3 = static_cast<uint32_t>(c3) << 0;
    return b0 | b1 | b2 | b3;
}

static void _write_big_endian_16(char* dst, uint16_t value)
{
    dst[0] = static_cast<char>(value >> 8);
    dst[1] = static_cast<char>(value);
}

static void _write_big_endian_32(char* dst, uint32_t value)
{
    dst[0] = static_cast<char>(value >> 24);
    dst[1] = static_cast<char>(value >> 16);
    dst[2] = static_cast<char>(value >> 8);
    dst[3] = static_cast<char>(value);
}

std::optional<TLVBlockHeader> TLVBlockHeader::Decode(
    const char buf[TLVBlockHeader::SIZE]
)
{
    // Multi-byte binary values are always written big-endian; decode them
    uint16_t type = _read_big_endian_16(buf);
    uint32_t block_size = _read_big_endian_32(buf + sizeof(uint16_t));

    // Zero-length blocks are explicitly disallowed
    if (block_size == 0)
    {
        return std::nullopt;
    }

    // Parse block type
    switch (type)
    {
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

void TLVBlockHeader::Encode(char buf[TLVBlockHeader::SIZE]) const
{
    _write_big_endian_16(buf, static_cast<uint16_t>(type));
    _write_big_endian_32(buf + sizeof(uint16_t), block_size);
}

size_t EncodeTLVBlock(char* dst, size_t n, TLVBlockType type, Block data)
{
    // We should not attempt to write empty blocks
    assert(n > 0 && "EncodeTLVBlock called with size 0");

    // Construct the header and validate that the buffer is large enough
    const TLVBlockHeader header{type, static_cast<uint32_t>(data.size())};
    const size_t num_bytes = TLVBlockHeader::SIZE + header.block_size;
    if (num_bytes > n)
    {
        return 0;
    }

    // Write the header into the start of the buffer
    header.Encode(dst);

    // Copy the entire block to the region just after the header
    std::memcpy(dst + TLVBlockHeader::SIZE, data.data(), data.size());
    return num_bytes;
}

platform::FilesystemResult<void>
EncodeTLVBlock(platform::IFileWriter& file, TLVBlockType type, Block block)
{
    // We should not attempt to write empty blocks
    assert(!block.empty());

    // Encode the header, representing type and size in big-endian byte order
    char header_buf[TLVBlockHeader::SIZE];
    const TLVBlockHeader header{type, static_cast<uint32_t>(block.size())};
    header.Encode(static_cast<char*>(header_buf));

    // Write the six-byte header to the file, propagating error if unsuccessful
    auto result = file.Write(static_cast<char*>(header_buf), sizeof(header_buf));
    if (!result)
    {
        return result;
    }

    // Write the block itself to the file
    return file.Write(block.data(), block.size());
}

static TLVBlockReadResult _propagate_filesystem_error(platform::FilesystemError err)
{
    if (err == platform::FilesystemError::IOError)
    {
        return TLVBlockReadResult{TLVBlockReadResultType::IOError};
    }
    return TLVBlockReadResult{TLVBlockReadResultType::ReadFailed};
}

TLVBlockReadResult
ReadTLVBlock(platform::IFileReader& file, std::vector<char>& out_block_data)
{
    // Read the next six bytes of the file, which should contain the next block's header
    char header_buf[TLVBlockHeader::SIZE];
    auto result = file.Read(static_cast<char*>(header_buf), sizeof(header_buf));
    if (!result)
    {
        return _propagate_filesystem_error(result.error());
    }

    // Decode the header, failing if the block type or size isn't valid
    std::optional<const TLVBlockHeader> header =
        TLVBlockHeader::Decode(static_cast<const char*>(header_buf));
    if (!header)
    {
        // There was no filesystem error, but we did not read a valid block
        return TLVBlockReadResult(TLVBlockReadResultType::Malformed);
    }

    // Ensure we can fit the block data in our reusable buffer, reallocating as needed
    out_block_data.reserve(QuantizeBufferSize(header->block_size));
    out_block_data.resize(header->block_size);

    // Read the next N bytes, where N is the block size indicated in the header
    result = file.Read(out_block_data.data(), out_block_data.size());
    if (!result)
    {
        return _propagate_filesystem_error(result.error());
    }
    if (result->num_bytes_read != header->block_size)
    {
        // We failed to read the number of bytes we needed
        return TLVBlockReadResult(TLVBlockReadResultType::Malformed);
    }

    // We got the whole block; construct a view into our reusable buffer and return a
    // successful result struct, propagating EOF
    Block block_data{out_block_data.data(), out_block_data.size()};
    assert(out_block_data.size() == header->block_size);
    return TLVBlockReadResult{*header, result->eof};
}

} // namespace datadog::impl
