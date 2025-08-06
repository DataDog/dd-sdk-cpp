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

namespace datadog::impl {

/**
 * Globally unique FourCC code that identifies a specific feature.
 */
using FeatureId = uint32_t;

/**
 * Encodes the given four characters into a uint32 in FourCC format, with the leftmost
 * character occupying the least significant byte of the resulting value. e.g.
 * CreateFeatureId("ABCD") => 0x44434241
 */
constexpr FeatureId CreateFeatureId(const char fourcc[5])
{
    const uint32_t a = static_cast<uint32_t>(fourcc[0]) << 0;
    const uint32_t b = static_cast<uint32_t>(fourcc[1]) << 8;
    const uint32_t c = static_cast<uint32_t>(fourcc[2]) << 16;
    const uint32_t d = static_cast<uint32_t>(fourcc[3]) << 24;
    return a | b | c | d;
}

/**
 * Callback invoked by a feature when it generates an event that needs to be enqueued
 * for storage. Passed to the Feature implementation by the Core on Start.
 *
 * @param event Arbitrary bytes. Must be non-empty. Will be copied into the storage
 *  queue. Eventually written to a batch with TLVBlockType::Event.
 * @param event_metadata Optional metadata to accompany the event; will be copied. When
 *  an event that has metadata is eventually written, the metadata will be prepended as
 *  a block of type TLVBlockType::Metadata.
 * @returns whether the event was successfuly enqueued for storage.
 */
using EventGeneratedFunc = std::function<bool(Block event, Block event_metadata)>;

/**
 * Lightweight wrapper for a block of TLV-formatted data read from a file. The data
 * pointed to by `block` will be an exact copy of an `event` or `event_metadata` value
 * previously enqueued for storage.
 */
struct TLVBlock
{
    TLVBlockType type;
    Block block;
};

/**
 * Reason that we failed to read the next TLVBlock from a batch file.
 */
enum class BatchReadError : uint8_t
{
    /**
     * File reads were OK, but the data is malformed: e.g. TLV block type was
     * unrecognized, TLV header showed a block size of 0, etc.
     */
    InvalidBlockFormat,
    /**
     * An attempt to read from the file was unsuccessful, e.g. because the file no
     * longer exists, our handle is invalid, etc.
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
        // vector, and return a TLVBlock object
        Block block{_block_data_buffer.data(), _block_data_buffer.size()};
        return TLVBlock{_current_type, block};
    }
};

/**
 * Lightweight description of an HTTP request that should be made in order to upload a
 * batch of event data for a specific feature.
 */
struct Report
{
    /**
     * Fully qualified URL (including query parameters if applicable) to use for the
     * request. All requests are assumed to be POSTs. Underlying string data must
     * persist throughout the lifetime of the HTTP request.
     */
    std::string_view url;
    /**
     * The set of headers to use with this request, in wire format (e.g. 'Foo: bar'),
     * newline-delimited, with a trailing newline.
     */
    std::string_view headers;
    /**
     * A function that will populate the body of the HTTP request on demand, allowing
     * large event payloads to be streamed directly to the HTTP layer without
     * intermediate copies.
     */
    platform::HttpBodyWriter body_writer;
};

/**
 * Describes how the storage thread should enforce size limits and other constraints on
 * the batch files for a specific feature.
 */
struct FeatureStorageConfig
{
    // TODO: Implement defaults, use these values in BatchWriter
};

/**
 * Base class used for the implementation of a feature. Implements user-facing API
 * operations, generates event payloads for storage, and processes batches of those
 * events for periodic upload.
 * 
 * The Core is only aware of the Feature interface, and it keeps track of registered
 * features via its own feature-agnostic data stucture, RegisteredFeature.
 * 
 * To implement a new feature:
 * 
 * - Establish a new source module `src/impl/features/foo`
 * - In that module, define `class Foo final : public Feature { ... };`
 * - Implement `GetId()` to return the globally unique FourCC code for that feature
 * - Implement `GetName()` to return the globally unique name for logs, storage, etc.
 * - If the feature has unique storage requirements, implement `GetStorageConfig()`
 * - If the feature needs initialization/shutdown logic, implement `Start()`/`Stop()`
 * - Define main-thread-callable functions for the required feature-specific operations
 * - In those functions, call `WriteEvent()` for each event the feature generate, using
 *    whatever binary format is appropriate for the feature
 * - Implement `UploadThread_PrepareReport()` to read batches of events (in the same
 *    format) and return a `Report` object describing the resulting HTTP request that
 *    should be made to upload that batch to the appropriate intake endpoint
 * 
 * Additionally, for the user-facing operations that your new feature exposes:
 *
 * - Create `include-c/datadog/foo.h` and declare the feature's C API
 * - Add `#include "datadog/foo.h"` to `include-c/datadog.h`
 * - Create `src/c/foo.cpp` and implement bindings to the underlying implementation.
 * - Create `include-cpp/datadog/foo.hpp` and declare the feature's C++ API
 * - Add `#include "datadog/foo.hpp"` to `include-cpp/datadog.hpp`
 * - Create `src/cpp/foo.cpp` and implement bindings to the underlying implementation.
 */
class Feature
{
public:
    virtual ~Feature() = default;
    
    virtual FeatureId GetId() const = 0;
    virtual std::string_view GetName() const = 0;
    virtual FeatureStorageConfig GetStorageConfig() const { return {}; }

    void OnCoreStarted(EventGeneratedFunc writer);
    void OnCoreStopping();

protected:
    /**
     * Called from the main thread when the SDK has finished starting up. This is the
     * first point at which events may be generated.
     */
    virtual void Start() {}

    /**
     * Called from the main thread when the SDK is about to shut down. This is the last
     * point at which events may be generated.
     */
    virtual void Stop() {}

    /**
     * Callable from the main thread in order to produce a new event. Copies the given
     * event payload(s) into the storage queue so that the storage thread can write them
     * to persistent storage. Once the batch containing this event is ready for upload,
     * the upload thread will pass it to UploadThread_PrepareReport().
     * 
     * @returns whether the event was successfully enqueued for storage. If called
     *  before Start() or after Stop(), always returns false.
     */
    bool WriteEvent(Block event, Block event_metadata = {});

    /**
     * @returns whether the feature has received a call to Start() and has not yet
     *  received a call to Stop().
     */
    bool IsRunning() const;

public:
    /**
     * Called from the upload thread when a batch of events written to storage by this
     * feature is ready to be processed and uploaded.
     */
    virtual std::optional<Report> UploadThread_PrepareReport(
        const CoreContext& context,
        BatchReader& reader
    ) = 0;

private:
    EventGeneratedFunc _event_callback;
};

}
