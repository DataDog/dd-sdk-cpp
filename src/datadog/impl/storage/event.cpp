// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/event.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/tlv.hpp"
#include "datadog/impl/core/types.hpp"
#include "datadog/impl/storage/util.hpp"

// Global version number applied to all event data stored persistently; may be bumped in
// the event of breaking changes in order to abandon previously-written events on disk.
// This versioning scheme applies to the storage implementation as a whole: individual
// features should implement their own versioning schemes internally if needed.
#define DATADOG_EVENT_STORAGE_VERSION "1"  // NOLINT(cppcoreguidelines-macro-usage)

namespace datadog::impl {

// Use (e.g.) 'v1' to store events gathered while tracking consent is granted;
// 'intermediate-v1' for events gathered while tracking consent is pending
const char* EventStorage::PENDING_SUBDIRECTORY_NAME =
    "intermediate-v" DATADOG_EVENT_STORAGE_VERSION;
const char* EventStorage::GRANTED_SUBDIRECTORY_NAME = "v" DATADOG_EVENT_STORAGE_VERSION;

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

EventStorageConfig EventStorageConfig::FromBatchSize(BatchSize batch_size) {
  const Duration max_file_age = BatchSize_ToMaxFileAgeForWrite(batch_size);
  return EventStorageConfig(max_file_age);
}

EventStorage::EventStorage(
    IFilesystem& fs,
    std::string_view feature_name,
    const DiagnosticLogger& logger,
    TrackingConsent initial_consent,
    const platform::IClock& clock,
    EventStorageConfig config
)
    : _fs(fs),
      _feature_name(feature_name),
      _logger(logger),
      _consent(initial_consent),
      _clock(clock),
      _config(config) {}

EventStorage::~EventStorage() {
  if (_current_file != INVALID_FILE_HANDLE) {
    _fs.Close(_current_file);
    _current_file = INVALID_FILE_HANDLE;
  }
}

void EventStorage::Flush() {
  if (_current_file != INVALID_FILE_HANDLE) {
    _fs.Close(_current_file);
    _current_file = INVALID_FILE_HANDLE;
    _current_file_details.Reset(0);
  }
}

bool EventStorage::Initialize(std::string_view events_root) {
  PlatformPath path;

  const char* join_message =
      "Failed to initialize event storage: path exceeded length limit";
  const char* mkdir_message =
      "Failed to initialize event storage: could not create directory";

  // Use _pending_path temporarily to hold <events_root>/<feature_name>; we'll
  // overwrite it below once we've derived the actual pending subdirectory path
  if (!JoinPaths(_pending_path, events_root, _feature_name, _logger, join_message)) {
    return false;
  }
  if (!EnsureDirectoryExists(_pending_path, path, _fs, _logger, mkdir_message)) {
    return false;
  }

  // <feature>/v1/ — events to be uploaded (upload thread reads from here)
  if (!JoinPaths(
          _granted_path,
          _pending_path.Get(),
          GRANTED_SUBDIRECTORY_NAME,
          _logger,
          join_message
      )) {
    return false;
  }
  if (!EnsureDirectoryExists(_granted_path, path, _fs, _logger, mkdir_message)) {
    return false;
  }

  // <feature>/intermediate-v1/ — events collected while consent is pending
  if (!JoinPaths(
          _pending_path,
          _pending_path.Get(),
          PENDING_SUBDIRECTORY_NAME,
          _logger,
          join_message
      )) {
    return false;
  }
  if (!EnsureDirectoryExists(_pending_path, path, _fs, _logger, mkdir_message)) {
    return false;
  }

  return true;
}

bool EventStorage::SetConsent(TrackingConsent value) {
  // Ignore spurious change events
  if (_consent == value) {
    // Value change accepted successfully as a no-op
    return true;
  }

  _consent = value;

  // Clear all state that pertains to the directory where we've been writing files, as
  // we may be switching to a new directory
  _last_known_filenames.clear();
  if (_current_file != INVALID_FILE_HANDLE) {
    _fs.Close(_current_file);
    _current_file = INVALID_FILE_HANDLE;
  }
  _current_file_details.Reset(0);

  // If tracking consent has been revoked, delete all pending data
  if (value == TrackingConsent::NotGranted) {
    // Allow data that was collected while consent was granted to be drained and
    // uploaded; no new data will be fed in
    return DeletePendingBatches();
  }

  // If tracking consent has been granted, migrate all event data from the pending
  // directory to the granted directory, so the upload thread will be able to read it
  if (value == TrackingConsent::Granted) {
    return MigratePendingBatchesToGranted();
  }

  // If tracking consent has been changed back to pending, do nothing: the storage
  // thread will write future events to the pending directory, and the upload thread
  // will drain the granted directory
  return true;
}

bool EventStorage::Write(Block event, Block event_metadata) {
  DATADOG_ASSERT(!event.empty(), "Write received empty event");

  // Branch on tracking consent to determine the appropriate place for the new event
  switch (_consent) {
    // If consent has been granted, write to the directory that the upload thread is
    // reading from
    case TrackingConsent::Granted:
      return WriteEventTo(_granted_path, event, event_metadata);

    // If consent is pending, write to an intermediate directory so that data is
    // captured locally but not yet uploaded
    case TrackingConsent::Pending:
      return WriteEventTo(_pending_path, event, event_metadata);

    // If consent has been explicitly revoked, store no data
    case TrackingConsent::NotGranted:
      return true;  // Event successfully handled as a no-op
  }
  DATADOG_ASSERT(false, "unhandled TrackingConsent enum value");
  return false;
}

bool EventStorage::DeletePendingBatches() {
  // Thread-safety/synchronization considerations:
  //
  // 1. We only delete from _pending_path, which the upload thread never reads
  //    from, so we don't have to worry about contention with other threads
  // 2. The storage thread performs deletion synchronously, so there can be no file
  //    writes happening concurrently during deletion
  // 3. When the storage thread does perform writes, it does so atomically and closes
  //    the file, so there are no open handles to any of the files we're deleting

  // Retrieve an up-to-date list of filenames in the target directory: we assume
  // exclusive access to _pending_path for the duration of this operation, so this
  // set of files should not change on disk except in response to our Delete calls
  if (!CacheKnownFilenames(_pending_path)) {
    _logger.Error(
        "Could not delete batch files on consent change: failed to read filenames from "
        "directory"
    );
    return false;
  }

  // Iterate over all filenames, attempting to delete each file
  for (const std::string& filename : _last_known_filenames) {
    // Build the full path to this file using _file_path as scratch space
    if (!JoinPaths(
            _file_path,
            _pending_path.Get(),
            filename,
            _logger,
            "Could not delete batch file on consent change: path too long"
        )) {
      return false;
    }

    PlatformPath pp;
    if (!pp.Encode(_file_path.CStr())) {
      _logger.Error(
          "Could not delete batch file on consent change: path encoding failed",
          {{"filename", filename}}
      );
      return false;
    }

    // Call Delete, and continue if successfully deleted
    auto result = _fs.Delete(pp);
    if (result == FilesystemResult::OK) {
      _logger.Debug(
          "Deleted batch file due to consent change", {{"filename", filename}}
      );
      continue;
    }

    // If we couldn't delete the file, abort the operation
    _logger.Error(
        "Could not delete batch file on consent change",
        {{"filename", filename}, {"error", FilesystemResultStr(result)}}
    );
    return false;
  }

  return true;
}

bool EventStorage::MigratePendingBatchesToGranted() {
  // Thread-safety/synchronization considerations:
  //
  // 1. We move files from _pending_path: as with DeletePendingBatches(), this
  //    happens synchronously on the storage thread, so no concurrent writes or open
  //    file handles exist
  // 2. We move files into _granted_path: the upload thread may be reading from this
  //    directory while we move files into it; however, since _pending_path and
  //    _granted_path are subdirectories within the same SDK-controlled directory,
  //    they are guaranteed to be on the same filesystem, and therefore a rename is
  //    atomic on both POSIX and Windows
  //
  // In the unlikely event of a filename conflict (clock adjustments, external
  // tampering, or rapid consent changes), we delete the source file and preserve the
  // file already in _granted_path.

  if (!CacheKnownFilenames(_pending_path)) {
    _logger.Error(
        "Could not migrate batch files on consent change: failed to read filenames "
        "from source directory"
    );
    return false;
  }

  // Iterate over all filenames, attempting to move each file
  for (const std::string& filename : _last_known_filenames) {
    // Build source path using _file_path as scratch space
    if (!JoinPaths(
            _file_path,
            _pending_path.Get(),
            filename,
            _logger,
            "Could not migrate batch file on consent change: path too long"
        )) {
      return false;
    }
    PlatformPath src_pp;
    if (!src_pp.Encode(_file_path.CStr())) {
      _logger.Error(
          "Could not migrate batch file on consent change: path encoding failed",
          {{"filename", filename}}
      );
      return false;
    }

    // Build destination path using a local StoragePath
    StoragePath dst_path;
    if (!JoinPaths(
            dst_path,
            _granted_path.Get(),
            filename,
            _logger,
            "Could not migrate batch file on consent change: path too long"
        )) {
      return false;
    }
    PlatformPath dst_pp;
    if (!dst_pp.Encode(dst_path.CStr())) {
      _logger.Error(
          "Could not migrate batch file on consent change: path encoding failed",
          {{"filename", filename}}
      );
      return false;
    }

    // Attempt to rename the file from pending to granted
    auto move_result = _fs.Rename(src_pp, dst_pp);
    if (move_result == FilesystemResult::OK) {
      _logger.Debug(
          "Migrated batch file due to consent change", {{"filename", filename}}
      );
      continue;
    }

    // If the move failed because a destination file exists with the same name, attempt
    // to resolve the conflict by deleting the source file and proceeding
    if (move_result == FilesystemResult::AlreadyExists) {
      // If we can successfully delete the source file (leaving the existing destination
      // file as-is), we can continue with the operation
      auto delete_result = _fs.Delete(src_pp);
      if (delete_result == FilesystemResult::OK) {
        _logger.Debug(
            "Deleted pending-directory copy of duplicate batch file",
            {{"filename", filename}}
        );
        continue;
      }

      // Otherwise, we have a file conflict and we're unable to delete the source file:
      // leave the file in place, but log a warning and carry on with the migration
      _logger.Warning(
          "Could not delete pending-directory copy of duplicate batch file",
          {{"filename", filename}, {"error", FilesystemResultStr(delete_result)}}
      );
      continue;
    }

    // If the move failed for any other reason, abort the migration operation
    _logger.Error(
        "Could not migrate batch file on consent change",
        {{"filename", filename}, {"error", FilesystemResultStr(move_result)}}
    );
    return false;
  }

  return true;
}

bool EventStorage::WriteEventTo(
    const StoragePath& dir_path, Block event, Block event_metadata
) {
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

  // If the encoded size of this write exceeds the maximum configured size for a single
  // batch file, reject the event outright: even a brand new batch file could not
  // contain it
  if (num_bytes > _config.max_file_size) {
    _logger.Error("Event dropped; size of single event exceeds max batch file size");
    return false;
  }

  // Determine which file we should write to, and abort if we were unable to resolve an
  // appropriate writable file
  if (!PrepareFileForNextWrite(dir_path, event, event_metadata)) {
    _logger.Error("Event dropped; could not prepare batch file for write");
    return false;
  }

  // Concatenate all data into a single contiguous region for an atomic write
  const size_t buffer_capacity = QuantizeBufferSize(num_bytes);
  _write_buffer.reserve(buffer_capacity);
  _write_buffer.resize(num_bytes);

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

  // Write the event block
  const size_t event_tlv_size = EncodeTLVBlock(
      write_addr, write_buffer_end - write_addr, TLVBlockType::Event, event
  );
  DATADOG_ASSERT(event_tlv_size > 0, "Failed to write TLV event block to buffer");
  write_addr += event_tlv_size;  // NOLINT(clang-analyzer-deadcode.DeadStores)
  DATADOG_ASSERT(write_addr == write_buffer_end, "Unexpected number of bytes written");

  // Perform a single atomic write to ensure that header + data (and metadata + event,
  // if applicable) are written together, all-or-nothing
  const auto write_result =
      _fs.Write(_current_file, _write_buffer.data(), _write_buffer.size());
  if (write_result.value != FilesystemResult::OK) {
    _logger.Error("Event dropped; write to batch file failed");
    return false;
  }

  // Write successful; update our current-file state
  _current_file_details.num_writes++;
  _current_file_details.num_bytes_written += num_bytes;
  return true;
}

bool EventStorage::PrepareFileForNextWrite(
    const StoragePath& dir_path, Block event, Block event_metadata
) {
  // Check our last-used file's age, size, etc. to see if we can reuse it
  const Timestamp current_time = _clock.Now();
  if (CanReuseFileForNextWrite(current_time, event, event_metadata)) {
    return true;
  }

  // If not, close the current file (if open) before opening a new one
  if (_current_file != INVALID_FILE_HANDLE) {
    _fs.Close(_current_file);
    _current_file = INVALID_FILE_HANDLE;
  }

  // Figure out what to name the new file
  const auto next = GetFilenameForNextWrite(dir_path, current_time);
  if (!next) {
    // Failed to resolve new filename (i.e. listing directory contents failed, or all
    // potential filenames are in use); can't proceed with file creation
    return false;
  }

  // Build the full path to the new batch file
  if (!JoinPaths(
          _file_path,
          dir_path.Get(),
          next->second,
          _logger,
          "Failed to prepare batch file for write: path too long"
      )) {
    return false;
  }

  PlatformPath pp;
  if (!pp.Encode(_file_path.CStr())) {
    return false;
  }

  // Open the file for write (not append mode, no advisory lock): new batch files
  // always start empty, and we keep the handle open across multiple writes
  auto open_result =
      _fs.OpenForWrite(pp, /*append=*/false, /*hold_advisory_lock=*/false);
  if (open_result.value != FilesystemResult::OK) {
    return false;
  }

  // Track the new file and reset file-state
  _current_file = open_result.handle;
  _current_file_details.Reset(next->first);
  return true;
}

bool EventStorage::CanReuseFileForNextWrite(
    Timestamp current_time, Block event, Block event_metadata
) const {
  // If we have no current file, we need a new one
  if (_current_file == INVALID_FILE_HANDLE) {
    return false;
  }

  // If the current file is older than our maximum age for a writable file, leave it
  // alone and start a new file
  const Timestamp presumed_creation_time =
      _ms_to_timestamp(_current_file_details.filename_ms);
  const Duration presumed_age = current_time - presumed_creation_time;
  if (presumed_age > _config.max_file_age) {
    return false;
  }

  // If we've reached our hard limit on the number of events recorded in a single file,
  // it's time to start a new batch
  if (_current_file_details.num_writes >= _config.max_writes_per_file) {
    return false;
  }

  // Compute the number of bytes we'll want to append to the current file, but take
  // caution not to use sizeof(TLVBlockHeader), which includes struct padding etc.;
  // always use TLVBlockHeader::SIZE when computing serialized size
  size_t num_bytes_to_write = TLVBlockHeader::SIZE + event.size();
  if (!event_metadata.empty()) {
    num_bytes_to_write += TLVBlockHeader::SIZE + event_metadata.size();
  }

  // If the file would exceed our hard limit on file size after write, start a new one
  const size_t expected_size_after_write =
      _current_file_details.num_bytes_written + num_bytes_to_write;
  if (expected_size_after_write > _config.max_file_size) {
    return false;
  }

  return true;
}

std::optional<std::pair<uint64_t, std::string>> EventStorage::GetFilenameForNextWrite(
    const StoragePath& dir_path, Timestamp current_time
) const {
  // Our new file will use a filename that reflects the current timestamp, but it's
  // possible that a file already exists with that name, and we don't want to reuse any
  // existing files that we didn't create ourselves: so start by retrieving the list of
  // existing filenames in our target directory
  if (!CacheKnownFilenames(dir_path)) {
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

  // If we didn't find an exact match, there's no existing file with this name, so
  // we're good to use it
  if (it == _last_known_filenames.cend() || *it != filename) {
    return std::make_pair(filename_ms, std::string(filename));
  }

  // If the target filename is already in use, loop over all possible filenames that we
  // might use for the next ~100ms, until we land on one for which there is no collision
  // (or exhaust the search)
  for (uint64_t offset = 1; offset < 100; offset++) {
    filename_ms++;
    filename = _ms_to_string(filename_ms, buffer);

    it = std::find_if(it, _last_known_filenames.cend(), [=](const std::string& s) {
      return s == filename;
    });

    if (it == _last_known_filenames.cend()) {
      return std::make_pair(filename_ms, std::string(filename));
    }
  }

  // We somehow had 100 sequentially-named files in the target directory; give up on
  // file creation
  return std::nullopt;
}

bool EventStorage::CacheKnownFilenames(const StoragePath& dir_path) const {
  _last_known_filenames.clear();

  PlatformPath pp;
  if (!pp.Encode(dir_path.CStr())) {
    return false;
  }

  auto result = _fs.ListFiles(pp, _last_known_filenames);
  if (result != FilesystemResult::OK) {
    return false;
  }

  std::sort(_last_known_filenames.begin(), _last_known_filenames.end());
  return true;
}

}  // namespace datadog::impl
