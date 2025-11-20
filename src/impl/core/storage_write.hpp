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

#include "core/block.hpp"
#include "datadog/core.hpp"
#include "diagnostics.hpp"
#include "platform/clock.hpp"
#include "platform/filesystem.hpp"

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
 * storage.
 *
 * Wraps a subdirectory '<STORAGE_ROOT>/<FEATURE>/<CONSENT>' in which batches of event
 * data are stored.
 *
 * The BatchWriter keeps track of a the batch file that it most recently wrote to, and
 * on each write it makes decisions about when to close the file and start a new one,
 * what new files should be named, etc.
 */
class BatchWriter {
 private:
  /**
   * Interface used to emit local log messages, mostly to aid in debugging the behavior
   * of the storage thread.
   */
  DiagnosticLogger _diagnostic_logger;

  /**
   * The directory containing TLV-format batch files with event data for this feature
   * and tracking consent permutation.
   *
   * @note While the BatchWriter uniquely owns this IDirectory _interface_, it is not
   *  guaranteed exclusive access to the underlying directory on disk, as the upload
   *  thread may be reading from the same directory that a BatchWriter is writing to.
   *  Our primary mechanism for avoiding file contention between these threads is
   *  time-based: the storage thread won't write to files past a certain age, and the
   *  upload thread won't read from files until they reach a slightly older age than the
   *  write-cutoff threshold. Other synchronization mechanisms may also exist, e.g. for
   *  coordinated operations like moving or cleaning up files.
   */
  std::unique_ptr<platform::IDirectory> _directory;

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
   * Details of _last_file, if one is open. Reinitialized when we start a new batch.
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
      std::unique_ptr<platform::IDirectory>&& directory,
      const platform::IClock& clock,
      BatchWriterConfig config
  );
  bool Delete();
  bool MigrateTo(BatchWriter& other);

  /**
   * Handles an event generated by a feature, writing it to the appropriate event
   * storage directory based on the current tracking consent.
   */
  bool HandleWrite(Block event, Block event_metadata);

 private:
  platform::IFileWriter* PrepareFileForNextWrite(Block event, Block event_metadata);
  bool CanReuseFileForNextWrite(
      Timestamp current_time, Block event, Block event_metadata
  ) const;
  std::optional<std::pair<uint64_t, std::string>> GetFilenameForNextWrite(
      Timestamp current_time
  ) const;
  bool CacheKnownFilenames() const;
};

/**
 * Implements the logic used in the storage thread to store event data generated by a
 * specific feature.
 *
 * Wraps a subdirectory, '<STORAGE_ROOT>/<FEATURE>', which contains all the event data
 * for that feature. Event data is written in TLV format to files that represent batches
 * of data being prepared for upload.
 *
 * The EventStorage object manages two BatchWriters, one for the directory containing
 * events for which tracking consent has been granted, and another for events collected
 * while tracking consent is pending. EventStorage defers to the appropriate writer for
 * the current tracking consent state, and it coordinates migration/deletion of events
 * when tracking consent changes.
 */
class EventStorage {
 public:
  static const char* PENDING_SUBDIRECTORY_NAME;
  static const char* GRANTED_SUBDIRECTORY_NAME;

 private:
  TrackingConsent _consent;
  std::unique_ptr<BatchWriter> _pending;
  std::unique_ptr<BatchWriter> _granted;

 public:
  /**
   * Initializes new state for writing batches to the given directory.
   *
   * @param directory Non-owning reference to the directory where the EventStorage will
   *  list, create, and write to files. The lifetime of the IDirectory is guaranteed to
   *  extend beyond the lifetime of the EventStorage.
   */
  explicit EventStorage(
      TrackingConsent consent,
      std::unique_ptr<BatchWriter>&& pending,
      std::unique_ptr<BatchWriter>&& granted
  );

  /**
   * Notifies the storage thread that the SDK's tracking consent value has changed.
   */
  bool SetTrackingConsent(TrackingConsent value);

  /**
   * Writes the given event data to the appropriate batch file, in TLV format.
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
};

}  // namespace datadog::impl
