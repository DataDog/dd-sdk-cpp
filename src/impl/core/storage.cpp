#include "core/storage.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/core.hpp"
#include "core/tlv.hpp"
#include "platform/clock.hpp"

// Global version number applied to all event data stored persistently; may be bumped in
// the event of breaking changes in order to abandon previously-written events on disk.
// This versioning scheme applies to the storage implementation as a whole: individual
// features should implement their own versioning schemes internally if needed.
#define DATADOG_EVENT_STORAGE_VERSION "1" // NOLINT(cppcoreguidelines-macro-usage)

namespace datadog::impl {

// Use (e.g.) 'v1' to store events gathered while tracking consent is granted;
// 'intermediate-v1' for events gathered while tracking consent is pending
const char* EventStorage::PENDING_SUBDIRECTORY_NAME =
    "intermediate-v" DATADOG_EVENT_STORAGE_VERSION;
const char* EventStorage::GRANTED_SUBDIRECTORY_NAME = "v" DATADOG_EVENT_STORAGE_VERSION;

// Maximum possible base-10 digits in a uint64_t, without null terminator
static const size_t MAX_UINT64_DECIMAL_DIGITS = 20;

static std::string_view _timestamp_to_string(
    uint64_t timestamp_ms,
    std::array<char, MAX_UINT64_DECIMAL_DIGITS>& buffer
)
{
    // Populate the provided buffer with the string representation of our uint64_t,
    // using std::to_chars, which does NOT write a null terminator
    char* begin = buffer.data();
    char* end = begin + buffer.size();
    const auto result = std::to_chars(begin, end, timestamp_ms);

    // We require a fixed-size buffer large enough to fit any uint64_t, so conversion
    // should always succeed
    assert(result.ec == std::errc{} && "uint64 to string conversion failed");

    // Return a std::string_view, which does NOT require a null terminator, constructed
    // from our buffer
    const size_t len = result.ptr - begin;
    return std::string_view{ begin, len };
};

BatchWriter::BatchWriter(std::unique_ptr<platform::IDirectory>&& directory)
    : _directory(std::move(directory))
{}

bool BatchWriter::Delete() // NOLINT (TODO)
{
    // TODO: Implement
    return false;
}

bool BatchWriter::MigrateTo(BatchWriter& other) // NOLINT (TODO)
{
    // TODO: Implement
    (void)other;
    return false;
}

bool BatchWriter::HandleWrite(Block event, Block event_metadata)
{
    assert(!event.empty() && "HandleWrite received empty event");

    // Determine which file we should write to, and abort if we were unable to resolve
    // an appropriate writable file
    platform::IFileWriter* file = PrepareFileForNextWrite(event, event_metadata);
    if (!file)
    {
        std::cout << "[STORAGE] ERROR: Failed to prepare file for next write\n";
        return false;
    }

    // Compute the number of bytes needed to serialize our event block (tagged with a
    // TLV header) and, if applicable, the accompanying metadata block
    size_t num_bytes = TLVBlockHeader::SIZE + event.size();
    if (!event_metadata.empty())
    {
        num_bytes += TLVBlockHeader::SIZE + event_metadata.size();
    }

    // We maintain a reusable buffer to concatenate all this data into a single
    // contiguous region, so we can write it to file atomically: to avoid excessive
    // allocations, round up to a reasonable threshold
    const size_t buffer_capacity = QuantizeBufferSize(num_bytes);
    _write_buffer.reserve(buffer_capacity);
    _write_buffer.resize(num_bytes);

    // Write our data to the intermediate buffer in TLV format
    char* write_addr = _write_buffer.data();
    char* write_buffer_end = write_addr + _write_buffer.size();

    // If we have a metadata block, prepend it
    if (!event_metadata.empty())
    {
        const size_t metadata_tlv_size = EncodeTLVBlock(
            write_addr,
            write_buffer_end - write_addr,
            TLVBlockType::Metadata,
            event_metadata
        );
        assert(metadata_tlv_size > 0 && "Failed to write TLV metadata block to buffer");
        write_addr += metadata_tlv_size;
    }

    // Write the event block
    const size_t event_tlv_size = EncodeTLVBlock(
        write_addr, write_buffer_end - write_addr, TLVBlockType::Event, event
    );
    assert(event_tlv_size > 0 && "Failed to write TLV event block to buffer");
    write_addr += event_tlv_size;
    assert(write_addr == write_buffer_end && "Unexpected number of bytes written");

    // Perform a single atomic write to ensure that header + data (and metadata + event,
    // if applicable) are written together, all-or-nothing
    const auto ok = file->Write(_write_buffer.data(), _write_buffer.size());
    if (!ok)
    {
        // Write failed
        // TODO: We're ignoring FilesystemError::IOError (i.e. bad flag set on file
        // write), but with the close-after-write approach it doesn't seem that we need
        // to discriminate based on error type or do anything to the filesystem in
        // response to write errors: we've lost this event, but we'll just keep trying
        // to write future events normally
        std::cout << "[STORAGE] ERROR: Write operation failed\n";
        return false;
    }

    // Write successful; update our current-file state
    _last_file_details.num_writes++;
    _last_file_details.num_bytes_written += num_bytes;
    return true;
}

platform::IFileWriter*
BatchWriter::PrepareFileForNextWrite(Block event, Block event_metadata)
{
    // Check our last-used file's age, size, etc. to see if we can reuse it
    const uint64_t current_time_nanos = platform::Clock::read_utc_nanos();
    const uint64_t current_time_ms = current_time_nanos / 1000000;
    if (CanReuseFileForNextWrite(current_time_ms, event, event_metadata))
    {
        return _last_file.get();
    }

    // If not, prepare to write a new file: start by figuring out what to name it
    const auto next = GetFilenameForNextWrite(current_time_ms);
    if (!next)
    {
        // Failed to resolve new filename (i.e. listing directory contents failed, or
        // all potential filenames are in use); can't proceed with file creation
        _last_file = nullptr;
        return nullptr;
    }

    // Defer to our filesystem implementation to get an interface for writing to this
    // file
    auto file = _directory->PrepareForWrite(next->second);
    if (!file)
    {
        // Failed to prepare file; can't proceed with write
        _last_file = nullptr;
        return nullptr;
    }

    // Reset our state to reflect that we have a new file, then return a non-owning
    // pointer to that file
    _last_file = std::move(*file);
    _last_file_details.Reset(next->first);
    return _last_file.get();
}

bool BatchWriter::CanReuseFileForNextWrite(
    uint64_t current_time_ms,
    Block event,
    Block event_metadata
) const
{
    // TODO: Specify constants; derive max age cutoff from BatchSize
    const int64_t max_file_age_for_write_ms = 2850;
    const int max_writes_per_file = 500;
    const size_t max_file_size_in_bytes = 0x400000;

    // If we have no current file, we need a new one
    if (!_last_file)
    {
        return false;
    }

    // If the current file is older than our maximum age for a writable file, leave it
    // alone and start a new file
    const uint64_t presumed_creation_time = _last_file_details.filename_ms;
    const int64_t presumed_age_ms =
        (static_cast<int64_t>(current_time_ms) -
         static_cast<int64_t>(presumed_creation_time));
    if (presumed_age_ms > max_file_age_for_write_ms)
    {
        return false;
    }

    // If we've reached our hard limit on the number of events recorded in a single
    // file, it's time to start a new batch
    if (_last_file_details.num_writes >= max_writes_per_file)
    {
        return false;
    }

    // Compute the number of bytes we'll want to append to the current file, but take
    // caution not to use sizeof(TLVBlockHeader), which includes struct padding etc.;
    // always use TLVBlockHeader::SIZE when computing serialized size
    size_t num_bytes_to_write = TLVBlockHeader::SIZE + event.size();
    if (!event_metadata.empty())
    {
        num_bytes_to_write += TLVBlockHeader::SIZE + event_metadata.size();
    }

    // If the file would exceed our hard limit on file size after write, it's time to
    // call it quits on that file and start a new one
    const size_t expected_size_after_write =
        _last_file_details.num_bytes_written + num_bytes_to_write;
    if (expected_size_after_write > max_file_size_in_bytes)
    {
        return false;
    }

    // We have a previously-used file that's still young enough and small enough to
    // continue writing to; allow it to be reused
    return true;
}

std::optional<std::pair<uint64_t, std::string>> BatchWriter::GetFilenameForNextWrite(
    uint64_t current_time_ms
) const
{
    // Our new file will use a filename that reflects the current timestamp, but it's
    // possible that a file already exists with that name, and we don't want to reuse
    // any existing files that we didn't create ourselves: so start by retrieving the
    // list of existing filenames in our target directory
    if (!CacheKnownFilenames())
    {
        // Failed to list directory contents; can't proceed with file creation
        return std::nullopt;
    }

    // Use stack memory to convert uint64 to string for candidate filenames
    std::array<char, MAX_UINT64_DECIMAL_DIGITS> buffer{ 0 };

    // Run an initial binary search to find the position in our sorted filenames array
    // where this entry would theoretically be inserted
    uint64_t filename_ms = current_time_ms;
    std::string_view filename = _timestamp_to_string(filename_ms, buffer);
    std::vector<std::string>::const_iterator it = std::lower_bound(
        _last_known_filenames.cbegin(), _last_known_filenames.cend(), filename
    );

    // If we didn't find an exact match, there's no existing file with this name, so
    // we're good to use it
    if (it == _last_known_filenames.cend() || *it != filename)
    {
        // Return a copy of the filename string
        return std::make_pair(filename_ms, std::string(filename));
    }

    // If the target filename is already in use, loop over all possible filenames that
    // we might use for the next ~100ms, until we land on one for which there is no
    // collision (or exhaust the search)
    for (uint64_t offset = 1; offset < 100; offset++)
    {
        // Compute the next filename in the series, incrementing by 1 millisecond
        filename_ms++;
        filename = _timestamp_to_string(filename_ms, buffer);

        // Perform a linear search from our current iterator position to the end of the
        // filename vector, to see if this next filename is also in use
        it = std::find_if(
            it,
            _last_known_filenames.cend(),
            [=](const std::string& s)
            {
                return s == filename;
            }
        );

        // If we got no match, we're good to use the current filename
        if (it == _last_known_filenames.cend())
        {
            // Return a copy of the filename string
            return std::make_pair(filename_ms, std::string(filename));
        }

        // Otherwise, our iterator has advanced, so the next loop will test the next
        // candidate filename in a smaller linear search space
    }

    // We somehow had 100 sequentially-named files in the target directory; give up on
    // file creation
    return std::nullopt;
}

bool BatchWriter::CacheKnownFilenames() const
{
    _last_known_filenames.clear();
    if (!_directory->ListFiles(_last_known_filenames))
    {
        return false;
    }
    std::sort(_last_known_filenames.begin(), _last_known_filenames.end());
    return true;
}

EventStorage::EventStorage(
    TrackingConsent consent,
    std::unique_ptr<BatchWriter>&& pending,
    std::unique_ptr<BatchWriter>&& granted
)
    : _consent(consent)
    , _pending(std::move(pending))
    , _granted(std::move(granted))
{}

bool EventStorage::SetTrackingConsent(TrackingConsent value)
{
    // Ignore spurious change events
    if (_consent == value)
    {
        // Value change accepted successfully as a no-op
        return true;
    }

    // Store the new value
    _consent = value;

    // If tracking consent has been revoked, delete all pending data
    if (value == TrackingConsent::NotGranted)
    {
        // Allow data that was collected while consent was granted to be drained and
        // uploaded; no new data will be fed in
        return _pending->Delete();
    }

    // If tracking consent has been granted, migrate all event data from the pending
    // directory to the granted directory, so the upload thread will be able to read it
    if (value == TrackingConsent::Granted)
    {
        return _pending->MigrateTo(*_granted);
    }

    // If tracking consent has been changed back to pending, do nothing: the storage
    // thread will write future events to the pending directory, and the upload thread
    // will drain the granted directory
    return true;
}

bool EventStorage::HandleWrite(Block event, Block event_metadata)
{
    assert(!event.empty() && "HandleWrite received empty event");

    // TEMP
    std::ostringstream oss;
    if (!event_metadata.empty())
    {
        oss << " <" << event_metadata << ">";
    }
    std::cout << "[STORAGE] Got write from feature: " << event << oss.str() << "\n";

    // Branch on tracking consent to determine the appropriate place for the new event
    switch (_consent)
    {
        // If consent has been granted, write to the directory that the upload thread is
        // reading from
        case TrackingConsent::Granted:
            return _granted->HandleWrite(event, event_metadata);

        // If consent is pending, write to an intermediate directory so that data is
        // captured locally but not yet uploaded
        case TrackingConsent::Pending:
            return _pending->HandleWrite(event, event_metadata);

        // If consent has been explicitly revoked, store no data
        case TrackingConsent::NotGranted:
            return true; // Event successfully handled as a no-op
    }
}

static void _handle_tracking_consent_changed(
    std::vector<RegisteredFeature>& features,
    const StorageMessage_TrackingConsentChanged& m
)
{
    for (auto& feature : features)
    {
        if (!feature.event_storage->SetTrackingConsent(m.value))
        {
            std::cout << "[STORAGE] ERROR: failed to handle tracking consent change "
                         "for feature "
                      << feature.name << "\n";
        }
    }
}

static void _handle_event_generated(
    std::vector<RegisteredFeature>& features,
    const StorageMessage_EventGenerated& m
)
{
    // Find the feature implementation identified in the message
    const auto feature = std::find_if(
        features.begin(),
        features.end(),
        [&](const RegisteredFeature& f)
        {
            return f.id == m.feature_id;
        }
    );

    // Ignore the message if no such feature exists
    if (feature == features.end())
    {
        assert(false && "feature_id on generated event matches no registered feature");
        return;
    }

    // No RegisteredFeature should ever be initialized without a valid EventStorage
    assert(
        feature->event_storage &&
        "feature identified by generated event does not have a valid EventStorage"
    );

    // Use the RegisteredFeature's EventStorage to process the write operation, only
    // continuing once the filesystem write operations return
    const bool write_ok = feature->event_storage->HandleWrite(
        Block(reinterpret_cast<const char*>(m.event.data()), m.event.size()), // NOLINT
        Block(
            reinterpret_cast<const char*>(m.event_metadata.data()), // NOLINT
            m.event_metadata.size()
        )
    );

    // If the write operation failed, note the error, drop the event, and continue
    if (!write_ok)
    {
        std::cout << "[STORAGE] ERROR: write failed\n";
    }
}

void StorageThreadMain(
    Queue<StorageMessage>& queue,
    std::vector<RegisteredFeature>& features
)
{
    std::cout << "[STORAGE] Started\n";

    // Perform continual blocking reads until we get std::nullopt, indicating that the
    // queue is drained and processing should stop
    while (const auto item = queue.Pop())
    {
        switch (item->type)
        {
            case StorageMessageType::TrackingConsentChanged:
                _handle_tracking_consent_changed(
                    features, item->payload.tracking_consent_changed
                );
                break;

            case StorageMessageType::EventGenerated:
                _handle_event_generated(features, item->payload.event_generated);
                break;
        }
    }

    std::cout << "[STORAGE] Finished\n";
}

} // namespace datadog::impl
