// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "datadog/core.hpp"

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/clock.hpp"
#include "datadog/impl/platform/filesystem.hpp"

namespace datadog::impl {

/**
 * Controls how often we create new batch files, based on both time and size limits.
 */
struct BatchWriterConfig {
  /**
   * Once a file exceeds this age, we will not write to it again.
   */
  Duration max_file_age;
  /**
   * If the next write to the current file would cause it to exceed this size (in
   * bytes), we will not write to that file and instead create a new one.
   */
  size_t max_file_size{0x400000};  // 4 MB
  /**
   * Maximum number of events that we will write to a single file. A single write
   * operation may include both metadata and event blocks: i.e. if configured with a
   * maximum of 500 writes, a file will contain no more than 500 TLV Metadata blocks and
   * 500 TLV Event blocks.
   */
  int max_writes_per_file{500};

  explicit BatchWriterConfig(Duration in_max_file_age)
      : max_file_age(in_max_file_age) {}

  static BatchWriterConfig FromBatchSize(BatchSize batch_size);
};

/**
 * Implements the logic used in the storage thread to commit events to persistent
 * storage for a specific feature.
 *
 * Wraps two subdirectories:
 *
 * - '<STORAGE_ROOT>/<FEATURE>/intermediate-v1': Events are written to this directory,
 *   collected into batches, while tracking consent is Pending. The upload thread is not
 *   aware of this directory, and its contents may be deleted or moved in response to
 *   tracking consent changes.
 *
 * - '<STORAGE_ROOT>/<FEATURE>/v1': Events are written to this directory, collected into
 *   batches, while tracking consent is Granted. The upload thread reads batches from
 *   this directory.
 *
 * The BatchWriter keeps track of the batch file that it most recently wrote to, and
 * on each write it makes decisions about when to close the file and start a new one,
 * what new files should be named, etc.
 */
class BatchWriter {
 public:
  static const char* PENDING_SUBDIRECTORY_NAME;
  static const char* GRANTED_SUBDIRECTORY_NAME;

 private:
  /**
   * Interface used to emit local log messages, mostly to aid in debugging the behavior
   * of the storage thread.
   */
  DiagnosticLogger _diagnostic_logger;

  /**
   * Current tracking consent value for the SDK.
   */
  TrackingConsent _consent;

  /**
   * Handle to the directory that we should write events to while tracking consent is
   * pending.
   */
  std::unique_ptr<platform::IDirectory> _pending_directory;

  /**
   * Handle to the directory that we should write events to while tracking consent is
   * granted. When tracking consent changes to granted, existing files from
   * _pending_directory may be migrated into _granted_directory.
   */
  std::unique_ptr<platform::IDirectory> _granted_directory;

  /**
   * Clock used to determine file names and make decisions based on file age. MUST be
   * the same clock used by the upload thread.
   */
  const platform::IClock& _clock;

  /**
   * Specifies how large a batch file may get, how old it can get before we stop writing
   * to it, etc.
   */
  const BatchWriterConfig _config;

  /**
   * Names of all files that we found in this directory the last time we checked.
   */
  mutable std::vector<std::string> _last_known_filenames;

  /**
   * Pointer to the IFileWriter interface for the file that we most recently wrote event
   * data to.
   */
  std::unique_ptr<platform::IFileWriter> _last_file;

  /**
   * Details of _last_file, if one is open. Reinitialized when we start a new batch or
   * when tracking consent changes.
   *
   * @note We never reopen existing files such that we would need to reinitialize this
   *  state from a non-empty file. If the SDK is initialized and batch files from a
   *  prior run are still present in the storage directory, the storage thread makes no
   *  attempt to reopen them.
   */
  struct FileDetails {
    uint64_t filename_ms{0};
    int num_writes{0};
    size_t num_bytes_written{0};

    void Reset(uint64_t in_filename_ms) {
      filename_ms = in_filename_ms;
      num_writes = 0;
      num_bytes_written = 0;
    }
  };
  FileDetails _last_file_details;

  /**
   * Buffer used to concatenate TLV block data prior to writing, so that we can ensure
   * atomic writes: i.e. a TLV header will never be written without its corresponding
   * block data, and if a Metadata block is present, it will always be followed by the
   * corresponding Event block.
   *
   * We might be able to avoid allocations from this buffer if we loosened our
   * guarantees about batch file consistency and allowed partial writes.
   */
  std::vector<char> _write_buffer;

 public:
  explicit BatchWriter(
      const DiagnosticLogger& diagnostic_logger,
      TrackingConsent consent,
      std::unique_ptr<platform::IDirectory>&& pending_directory,
      std::unique_ptr<platform::IDirectory>&& granted_directory,
      const platform::IClock& clock,
      BatchWriterConfig config
  );

  /**
   * Notifies the storage thread that the SDK's tracking consent value has changed.
   */
  bool SetTrackingConsent(TrackingConsent value);

  /**
   * Handles an event generated by a feature, writing it to the appropriate event
   * storage directory based on the current tracking consent.
   *
   * @param event Binary data to be written as a TLV 'Event' block. Must have nonzero
   *  length; write attempts with no event data will always fail.
   * @param event_metadata Optional arbitrary metadata describing the event, to be
   *  written as a TLV 'Metadata' block immediately preceding the 'Event' block. If
   *  empty, no 'Metadata' block will be written. If both an 'Event' block and a
   *  'Metadata' block are to be written, they will always be written within the same
   *  file.
   *
   * @returns whether all requested data was successfully written. Atomic writes are
   *  guaranteed: on success, all requested data is written to the file; on failure, no
   *  data is written.
   */
  bool HandleWrite(Block event, Block event_metadata);

 private:
  /**
   * Attempts to delete all batch files in _pending_directory. Returns true if all files
   * were deleted successfully.
   */
  bool DeletePendingBatches();

  /**
   * Attempts to move all batch files from _pending_directory to _granted_directory. In
   * the event of a filename conflict, the copy of the file in _pending_directory is
   * deleted, leaving the file in _granted_directory untouched.
   *
   * Failure to delete the pending-directory file in case of conflict does not halt the
   * process; the file will be left in place and migration will continue.
   *
   * Returns true if all files with non-conflicting names were moved successfully.
   */
  bool MigratePendingBatchesToGranted();

  /**
   * Attempts to write the given event to a batch file within the given directory,
   * reusing _last_file if it exists and is still valid for writing to, and creating a
   * new file otherwise.
   */
  bool WriteEventTo(platform::IDirectory& directory, Block event, Block event_metadata);

  /**
   * Resolves the appropriate file that we should an event, whether that's _last_file or
   * a newly-created file. Repopulates _last_file, _last_file_details, etc.
   */
  platform::IFileWriter* PrepareFileForNextWrite(
      platform::IDirectory& directory, Block event, Block event_metadata
  );

  /**
   * Determines whether _last_file is still suitable for writing this event to.
   */
  bool CanReuseFileForNextWrite(
      Timestamp current_time, Block event, Block event_metadata
  ) const;

  /**
   * Designates a name to use for a newly-created batch file in the given directory,
   * returning both the integer millisecond count reflecting file creation time, as well
   * as the string-formatted version of that same value.
   */
  std::optional<std::pair<uint64_t, std::string>> GetFilenameForNextWrite(
      const platform::IDirectory& directory, Timestamp current_time
  ) const;

  /**
   * Retrieves a list of all files in the given directory, caching that set of names in
   * _last_known_filenames.
   */
  bool CacheKnownFilenames(const platform::IDirectory& directory) const;
};

}  // namespace datadog::impl
