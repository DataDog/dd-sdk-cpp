// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/storage_write.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"
#include "datadog/impl/core/storage/util.hpp"
#include "datadog/impl/core/storage_queue.hpp"
#include "datadog/impl/core/tlv.hpp"
#include "datadog/impl/core/types.hpp"
#include "datadog/impl/core/util/assert.hpp"

// Global version number applied to all event data stored persistently; may be bumped in
// the event of breaking changes in order to abandon previously-written events on disk.
// This versioning scheme applies to the storage implementation as a whole: individual
// features should implement their own versioning schemes internally if needed.
#define DATADOG_EVENT_STORAGE_VERSION "1"  // NOLINT(cppcoreguidelines-macro-usage)

namespace datadog::impl {

// Maximum possible base-10 digits in a uint64_t, without null terminator
static const size_t MAX_UINT64_DECIMAL_DIGITS = 20;

static std::string_view _ms_to_string(
    uint64_t timestamp_ms, std::array<char, MAX_UINT64_DECIMAL_DIGITS>& buffer
) {
  // Populate the provided buffer with the string representation of our uint64_t, using
  // std::to_chars, which does NOT write a null terminator
  char* begin = buffer.data();
  char* end = begin + buffer.size();
  const auto result = std::to_chars(begin, end, timestamp_ms);

  // We require a fixed-size buffer large enough to fit any uint64_t, so conversion
  // should always succeed
  DATADOG_ASSERT(result.ec == std::errc{}, "uint64 to string conversion failed");

  // Return a std::string_view, which does NOT require a null terminator, constructed
  // from our buffer
  const size_t len = result.ptr - begin;
  return std::string_view{begin, len};
}

static Timestamp _ms_to_timestamp(uint64_t timestamp_ms) {
  const int64_t count = static_cast<int64_t>(timestamp_ms);
  if (count < 0) {
    return Timestamp{};
  }
  return Timestamp{std::chrono::nanoseconds(count * 1000000)};
}

static uint64_t _timestamp_to_ms(Timestamp timestamp) {
  // Use raw milliseconds within the storage implementation, since we encode file
  // creation time in the filename with millisecond precision
  auto elapsed = timestamp.time_since_epoch();
  if (elapsed.count() < 0) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
}

BatchWriterConfig BatchWriterConfig::FromBatchSize(BatchSize batch_size) {
  const Duration max_file_age = BatchSize_ToMaxFileAgeForWrite(batch_size);
  return BatchWriterConfig(max_file_age);
}

BatchWriter::BatchWriter(
    const DiagnosticLogger& in_diagnostic_logger,
    TrackingConsent in_consent,
    IFilesystem& in_fs,
    FeatureEventStorage& in_storage,
    const platform::IClock& in_clock,
    BatchWriterConfig in_config
)
    : _diagnostic_logger(in_diagnostic_logger),
      _consent(in_consent),
      _fs(in_fs),
      _storage(in_storage),
      _clock(in_clock),
      _config(in_config) {}

bool BatchWriter::SetTrackingConsent(TrackingConsent value) {
  // Ignore spurious change events
  if (_consent == value) {
    // Value change accepted successfully as a no-op
    return true;
  }

  // Store the new value
  _consent = value;

  // Clear all state that pertains to the directory where we've been writing files, as
  // we may be switching to a new directory
  _last_known_filenames.clear();
  _active_file.Clear();

  // If tracking consent has been revoked, delete all pending data
  if (value == TrackingConsent::NotGranted) {
    // Allow data that was collected while consent was granted to be drained and
    // uploaded; no new data will be fed in
    return _storage.DeletePendingBatches();
  }

  // If tracking consent has been granted, migrate all event data from the pending
  // directory to the granted directory, so the upload thread will be able to read it
  if (value == TrackingConsent::Granted) {
    return _storage.MigratePendingBatchesToGranted();
  }

  // If tracking consent has been changed back to pending, do nothing: the storage
  // thread will write future events to the pending directory, and the upload thread
  // will drain the granted directory
  return true;
}

bool BatchWriter::HandleWrite(Block event, Block event_metadata) {
  DATADOG_ASSERT(!event.empty(), "HandleWrite received empty event");

  // Branch on tracking consent to determine the appropriate place for the new event
  switch (_consent) {
    // If consent is granted or pending, write the event to the latest batch file in the
    // appropriate directory
    case TrackingConsent::Granted:
    case TrackingConsent::Pending:
      return FlushEvent(event, event_metadata);

    // If consent has been explicitly revoked, store no data
    case TrackingConsent::NotGranted:
      return true;  // Event successfully handled as a no-op
  }
  DATADOG_ASSERT(false, "unhandled TrackingConsent enum value");
  return false;
}

bool BatchWriter::FlushEvent(Block event, Block event_metadata) {
  // Resolve the appropriate directory path for our current consent value
  if (_consent == TrackingConsent::NotGranted) {
    DATADOG_ASSERT(false, "attempted FlushEvent with consent NotGranted");
    return false;
  }
  const StoragePath& consent_dir_path = _consent == TrackingConsent::Granted
                                            ? _storage.GetGrantedPath()
                                            : _storage.GetPendingPath();

  // If we don't permit at least 1 write per file, reject all writes
  if (_config.max_writes_per_file <= 0) {
    return false;
  }

  // Compute the number of bytes needed to serialize our event block (tagged with a TLV
  // header) and, if applicable, the accompanying metadata block
  size_t num_bytes = TLVBlockHeader::SIZE + event.size();
  if (!event_metadata.empty()) {
    num_bytes += TLVBlockHeader::SIZE + event_metadata.size();
  }

  // If the encoded size of this write (metadata + event, with headers) exceeds the
  // maximum configured size for a single batch file, reject the event outright: even a
  // brand new batch file could not contain it
  if (num_bytes > _config.max_file_size) {
    _diagnostic_logger.Error(
        "Event dropped; size of single event exceeds max batch file size"
    );
    return false;
  }

  // Determine which file we should write to, and abort if we were unable to resolve an
  // appropriate writable file
  StoragePath* file_path =
      PrepareFileForNextWrite(consent_dir_path, event, event_metadata);
  if (!file_path) {
    // If this error occurs, it's likely due to an underlying I/O error, or else we're
    // flooding the storage thread with 100+ batches worth of event data in a very small
    // time span, such that there are no available timestamps left to use as filenames
    _diagnostic_logger.Error("Event dropped; could not prepare batch file for write");
    return false;
  }

  // Open the file for write: we re-open batch files on each write, closing after each
  // event written, in order to ensure that events are reliably flushed as soon as
  // they're handled by the storage thread
  const bool append = true;
  const bool hold_advisory_lock = false;
  auto open_res = FilesystemWrapper(_fs).OpenForWrite(
      file_path->CStr(), append, hold_advisory_lock
  );
  if (open_res.value != FilesystemResult::OK) {
    _diagnostic_logger.Error(
        "Event dropped; could not open batch file for write",
        {{"path", file_path->Get()}, {"error", FilesystemResultStr(open_res.value)}}
    );
    return false;
  }
  File& file = open_res.file;

  // We maintain a reusable buffer to concatenate all this data into a single contiguous
  // region, so we can write it to file atomically: to avoid excessive allocations,
  // round up to a reasonable threshold
  const size_t buffer_capacity = QuantizeBufferSize(num_bytes);
  _write_buffer.reserve(buffer_capacity);
  _write_buffer.resize(num_bytes);

  // Write our data to the intermediate buffer in TLV format
  char* write_addr = _write_buffer.data();
  char* write_buffer_end = write_addr + _write_buffer.size();

  // If we have a metadata block, prepend it
  if (!event_metadata.empty()) {
    const size_t metadata_tlv_size = EncodeTLVBlock(
        write_addr,
        write_buffer_end - write_addr,
        TLVBlockType::Metadata,
        event_metadata
    );
    DATADOG_ASSERT(
        metadata_tlv_size > 0, "Failed to write TLV metadata block to buffer"
    );
    write_addr += metadata_tlv_size;
  }

  // Encode the event block into our write buffer
  const size_t event_tlv_size = EncodeTLVBlock(
      write_addr, write_buffer_end - write_addr, TLVBlockType::Event, event
  );
  DATADOG_ASSERT(event_tlv_size > 0, "Failed to write TLV event block to buffer");
  write_addr += event_tlv_size;  // NOLINT(clang-analyzer-deadcode.DeadStores)
  DATADOG_ASSERT(write_addr == write_buffer_end, "Unexpected number of bytes encoded");

  // Perform a single atomic write to ensure that header + data (and metadata + event,
  // if applicable) are written together, all-or-nothing
  const auto write_res = file.Write(_write_buffer.data(), _write_buffer.size());
  if (write_res.value != FilesystemResult::OK) {
    // Write failed: log an error, drop the event, and carry on attempting to write
    // future events
    _diagnostic_logger.Error(
        "Event dropped; write to batch file failed",
        {{"path", file_path->Get()}, {"error", FilesystemResultStr(write_res.value)}}
    );
    return false;
  }

  // File::~File() will close the file handle automatically in the event of failure, but
  // close it explicitly after a successful write
  const auto close_res = file.Close();
  if (close_res != FilesystemResult::OK) {
    _diagnostic_logger.Warning(
        "Failed to close batch file after event write",
        {{"path", file_path->Get()}, {"error", FilesystemResultStr(close_res)}}
    );
  }

  // Write successful; update our current-file state
  _active_file.num_writes++;
  _active_file.num_bytes_written += write_res.bytes_written;
  return true;
}

StoragePath* BatchWriter::PrepareFileForNextWrite(
    const StoragePath& consent_dir_path, Block event, Block event_metadata
) {
  // Check our last-used file's age, size, etc. to see if we can reuse it
  const Timestamp current_time = _clock.Now();
  if (CanReuseFileForNextWrite(current_time, event, event_metadata)) {
    return &_active_file.path;
  }

  // If not, prepare to write a new file: start by figuring out what to name it
  const auto next = GetFilenameForNextWrite(consent_dir_path, current_time);
  if (!next) {
    // Failed to resolve new filename (i.e. listing directory contents failed, or all
    // potential filenames are in use); can't proceed with file creation
    _active_file.Clear();
    return nullptr;
  }
  const uint64_t next_filename_ms = next->first;
  const std::string& next_filename = next->second;

  // Reset our state to reflect that we have a new file, then return a non-owning
  // pointer to the buffer that holds the path to that file
  if (!_active_file.Reset(consent_dir_path, next_filename, next_filename_ms)) {
    return nullptr;
  }
  return &_active_file.path;
}

bool BatchWriter::CanReuseFileForNextWrite(
    Timestamp current_time, Block event, Block event_metadata
) const {
  // If we have no current file, we need a new one
  if (_active_file.path.Get().empty()) {
    return false;
  }

  // If the current file is older than our maximum age for a writable file, leave it
  // alone and start a new file
  const Timestamp presumed_creation_time = _ms_to_timestamp(_active_file.filename_ms);
  const Duration presumed_age = current_time - presumed_creation_time;
  if (presumed_age > _config.max_file_age) {
    return false;
  }

  // If we've reached our hard limit on the number of events recorded in a single file,
  // it's time to start a new batch
  if (_active_file.num_writes >= _config.max_writes_per_file) {
    return false;
  }

  // Compute the number of bytes we'll want to append to the current file, but take
  // caution not to use sizeof(TLVBlockHeader), which includes struct padding etc.;
  // always use TLVBlockHeader::SIZE when computing serialized size
  size_t num_bytes_to_write = TLVBlockHeader::SIZE + event.size();
  if (!event_metadata.empty()) {
    num_bytes_to_write += TLVBlockHeader::SIZE + event_metadata.size();
  }

  // If the file would exceed our hard limit on file size after write, it's time to call
  // it quits on that file and start a new one
  const size_t expected_size_after_write =
      _active_file.num_bytes_written + num_bytes_to_write;
  if (expected_size_after_write > _config.max_file_size) {
    return false;
  }

  // We have a previously-used file that's still young enough and small enough to
  // continue writing to; allow it to be reused
  return true;
}

std::optional<std::pair<uint64_t, std::string>> BatchWriter::GetFilenameForNextWrite(
    const StoragePath& consent_dir_path, Timestamp current_time
) const {
  // Our new file will use a filename that reflects the current timestamp, but it's
  // possible that a file already exists with that name, and we don't want to reuse any
  // existing files that we didn't create ourselves: so start by retrieving the list of
  // existing filenames in our target directory
  if (!CacheKnownFilenames(consent_dir_path)) {
    // Failed to list directory contents; can't proceed with file creation
    return std::nullopt;
  }

  // Use stack memory to convert uint64 to string for candidate filenames
  std::array<char, MAX_UINT64_DECIMAL_DIGITS> buffer{0};

  // Run an initial binary search to find the position in our sorted filenames array
  // where this entry would theoretically be inserted
  uint64_t filename_ms = _timestamp_to_ms(current_time);
  std::string_view filename = _ms_to_string(filename_ms, buffer);
  std::vector<std::string>::const_iterator it = std::lower_bound(
      _last_known_filenames.cbegin(), _last_known_filenames.cend(), filename
  );

  // If we didn't find an exact match, there's no existing file with this name, so we're
  // good to use it
  if (it == _last_known_filenames.cend() || *it != filename) {
    // Return a copy of the filename string
    return std::make_pair(filename_ms, std::string(filename));
  }

  // If the target filename is already in use, loop over all possible filenames that we
  // might use for the next ~100ms, until we land on one for which there is no collision
  // (or exhaust the search)
  for (uint64_t offset = 1; offset < 100; offset++) {
    // Compute the next filename in the series, incrementing by 1 millisecond
    filename_ms++;
    filename = _ms_to_string(filename_ms, buffer);

    // Perform a linear search from our current iterator position to the end of the
    // filename vector, to see if this next filename is also in use
    it = std::find_if(it, _last_known_filenames.cend(), [=](const std::string& s) {
      return s == filename;
    });

    // If we got no match, we're good to use the current filename
    if (it == _last_known_filenames.cend()) {
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

bool BatchWriter::CacheKnownFilenames(const StoragePath& consent_dir_path) const {
  // Ensure that our vector is empty before we attempt to populate it
  _last_known_filenames.clear();

  // Retrieve a directory listing (regular files only), caching the results
  FilesystemWrapper fsw(_fs);
  const auto res = fsw.ListFiles(consent_dir_path.CStr(), _last_known_filenames);
  if (res != FilesystemResult::OK) {
    _diagnostic_logger.Warning(
        "Failed to examine existing batch files on event write: unable to list files",
        {{"path", consent_dir_path.Get()}, {"error", FilesystemResultStr(res)}}
    );
    return false;
  }

  // Sort filenames for deterministic iteration in timestamp-name-order
  std::sort(_last_known_filenames.begin(), _last_known_filenames.end());
  return true;
}

}  // namespace datadog::impl
