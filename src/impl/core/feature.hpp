#pragma once

#include <cinttypes>
#include <string>
#include <functional>
#include <memory>

#include "nonstd/expected.hpp"

#include "platform/filesystem.hpp"
#include "platform/http.hpp"
#include "core/block.hpp"
#include "core/context.hpp"
#include "core/tlv.hpp"

/**
 * SCRATCH
 * 
 * Feature registers itself with the core, providing id, name, and:
 * 
 * - a function that will generate reports at the behest of the core, given a context
 *   and a batch of data read from disk
 * 
 * In exchange, the core gives the feature:
 * 
 * - an interface for writing data to storage
 * 
 * When a feature generates a new Block, the Block is passed to a background thread to
 * be immediately serialized to persistent storage (i.e. flushed to disk) in TLV format.
 * Multiple blocks are stored together in batches: each batch is a file. When a batch is
 * ready for upload, it is passed to the feature's report-generation implementation,
 * which reads each Block from the file and generates an HTTP request body that encodes
 * the data represented by the batch.
 */

namespace datadog::impl {

using FeatureId = uint32_t;

constexpr FeatureId CreateFeatureId(const char fourcc[5])
{
    const uint32_t a = static_cast<uint32_t>(fourcc[0]) << 0;
    const uint32_t b = static_cast<uint32_t>(fourcc[1]) << 8;
    const uint32_t c = static_cast<uint32_t>(fourcc[2]) << 16;
    const uint32_t d = static_cast<uint32_t>(fourcc[3]) << 24;
    return a | b | c | d;
}

/**
 * Callback that a feature implementation uses to enqueue a feature-specific event
 * payload to be written to a TLV file. event must be specified, and it will be written
 * to the latest batch as a block with the 'Event' type. If event_metadata is provided,
 * it will be serialized as a block of type 'Metadata', immediately preceding the
 * 'Event' block.
 * 
 * @param event A span of bytes representing the payload of the event. This
 *  typically represents the data that should be sent to Datadog intake when this
 *  event is eventually reported.
 * @param event_metadata An optional span of bytes representing an arbitrary set of
 *  data describing the event. This is typically used to inform additional logic
 *  used by the feature when preparing the batch for upload, but it does not
 *  typically encode data that's sent in the resulting HTTP request.
 */
using StorageWriter = std::function<bool(Block event, Block event_metadata)>;

struct TLVBlock
{
    TLVBlockType type;
    Block block;
};

enum class BatchReadError : uint8_t
{
    InvalidBlockFormat,
    FailedRead,
    IOError
};

class BatchReader
{
private:
    platform::IFileReader& _file;
    TLVBlockType _current_type;
    std::vector<char>& _block_data_buffer;

public:
    BatchReader(platform::IFileReader& file, std::vector<char>& buffer)
        : _file(file)
        , _current_type(TLVBlockType::Event)
        , _block_data_buffer(buffer)
    {
    }

    nonstd::expected<TLVBlock, BatchReadError> ReadNext()
    {
        // A filesystem error indicates we couldn't read from the file
        auto result = ReadTLVBlock(_file, _current_type, _block_data_buffer);
        if (!result)
        {
            // Signal IOError (bad bit set on read) explicitly
            if (result.error() == platform::FilesystemError::IOError)
            {
                return nonstd::make_unexpected(BatchReadError::IOError);
            }
            return nonstd::make_unexpected(BatchReadError::FailedRead);
        }

        // A return value of false means we read from the file but the data was not
        // semantically valid
        const bool read_ok = *result;
        if (!read_ok)
        {
            return nonstd::make_unexpected(BatchReadError::InvalidBlockFormat);
        }

        // Successful read; block is valid: construct a lightweight view of our member
        // vector, and return a lightweight TLVBlock object
        Block block{_block_data_buffer.data(), _block_data_buffer.size()};
        return TLVBlock{_current_type, block};
    }
};

class Report
{
    std::string_view url;
    std::string_view headers;
    platform::HttpBodyWriter body_writer;
};

struct FeatureStorageConfig
{
};

/**
 * Base class used for the implementation of a feature.
 * 
 * The Core is only aware of the FeatureBase interface, and it keeps track of registered
 * features via its own feature-agnostic data stucture. Each feature is implemented by
 * defining a subclass of FeatureBase and implementing the required member functions.
 *
 * Inheritance (with a one-level-deep hierarchy) is a natural fit for this pattern,
 * especially since each feature forms an architectural boundary with the Core. Overhead
 * due to dynamic dispatch is relatively insignificant, as virtual function calls only
 * occur on init/shutdown and when periodically preparing data for upload from a
 * background thread.
 */
class FeatureBase
{
public:
    virtual ~FeatureBase() = default;
    
    virtual FeatureId GetId() const = 0;
    virtual std::string_view GetName() const = 0;
    virtual FeatureStorageConfig GetStorageConfig() const { return {}; }

    void OnCoreStarted(StorageWriter writer);
    void OnCoreStopping();

    virtual std::optional<Report> PrepareReport(BatchReader& reader) = 0;

protected:
    virtual void Start() {}
    virtual void Stop() {}

    bool WriteEvent(Block event, Block event_metadata = {});
    bool IsRunning() const;

private:
    StorageWriter _writer;
};

}
