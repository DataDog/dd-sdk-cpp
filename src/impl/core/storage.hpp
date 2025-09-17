// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "core/block.hpp"
#include "core/feature.hpp"
#include "core/queue.hpp"
#include "platform/clock.hpp"

namespace datadog::platform {
class IDirectory;
class IFileWriter;
}  // namespace datadog::platform

namespace datadog::impl {

/**
 * Type identifier for an internal message that can be produced to the storage queue in
 * order to push data to the storage thread.
 *
 * @note If adding a new message type, take care to:
 *
 * 1. Define a payload type `struct StorageMessage_<NewTypeName>`
 * 2. Add that type as a member of the `StorageMessage::Payload` union
 * 3. Add a branch to `~StorageMessage()` to explicitly handle the destruction of
 *    `payload.<new_type_name>` when `type == StorageMessageType:<NewTypeName>`: if
 *    payload type is trivially destructible, static_assert so; if it owns memory or
 *    manages resources, invoke its destructor on `payload.<new_type_name>` explicitly.
 * 4. Add a branch to `StorageMessage(StorageMessage&&)` to handle move construction
 *    with payloads of the new type
 * 5. Add a branch to `operator= (StorageMessage&&)` to handle move-assignment with
 *    payloads of the new type
 * 6. Add a static function `StorageMessage <NewTypeName>(...)` to initialize messages
 *    of the new type.
 *
 * We might avoid this complexity by switching to std::variant, but this approach treats
 * message handling as performance-critical since message construction occurs in the
 * main thread.
 */
enum class StorageMessageType : uint8_t {
  TrackingConsentChanged,
  EventGenerated,
};

/**
 * An API call has changed the configured user tracking consent value.
 */
struct StorageMessage_TrackingConsentChanged {
  /**
   * Whether the user has consented to tracking.
   */
  TrackingConsent value;
};

/**
 * A feature implementation has produced an event that should be flushed to persistent
 * storage.
 */
struct StorageMessage_EventGenerated {
  /**
   * Unique FourCC identifier of the feature that produced this message. The
   * accompanying event data will be written in the storage directory that corresponds
   * to this feature.
   */
  FeatureId feature_id;
  /**
   * The binary data representing the payload of this event, to be written to storage as
   * a TLV block of type 'Event'. The message stores a copy of the source data.
   */
  std::vector<uint8_t> event;
  /**
   * An optional payload describing the event, also copied. Will be written as a TLV
   * block of type 'Metadata', immediately preceiding the Event block, if and only if
   * non-empty.
   */
  std::vector<uint8_t> event_metadata;

  explicit StorageMessage_EventGenerated(
      FeatureId in_feature_id, Block in_event, Block in_event_metadata
  )
      : feature_id(in_feature_id),
        event(in_event.begin(), in_event.end()),
        event_metadata(in_event_metadata.begin(), in_event_metadata.end()) {}
};

/**
 * A message sent to the storage thread.
 */
struct StorageMessage {
  /**
   * Actual data associated with a message; discriminated by StorageMessage::type.
   *
   * If adding a new payload type that is not trivially destructible, you MUST ensure
   * that `~StorageMessage()` manually invokes the destructor on the appropriate union
   * member when the message has the corresponding type.
   *
   * For clarity: if your payload type owns memory (or manages resources) such that its
   * destructor must be called when it goes out of scope (regardless of whether that
   * destructor is explicitly-defined or compiler-generated), then it is NOT trivially
   * destructible and you MUST invoke its destructor explicitly in `~StorageMessage()`.
   */
  union Payload {
    StorageMessage_TrackingConsentChanged tracking_consent_changed;
    StorageMessage_EventGenerated event_generated;

    /**
     * Messages are initialized using static functions that use placement new to
     * initialize payloads; let the compiler know that it's OK for a Payload to exist in
     * a non-initialized state.
     */
    Payload() {};  // NOLINT(cppcoreguidelines-pro-type-member-init)

    /**
     * Deletion of union members is explicitly handled by ~StorageMessage(); define an
     * empty union destructor to signal this fact to the compiler.
     */
    ~Payload() {};

    // Payload itself should have no compiler-generated copy/move operations
    Payload(const Payload&) = delete;
    Payload(Payload&&) = delete;
    Payload& operator=(const Payload&) = delete;
    Payload& operator=(Payload&&) = delete;
  };

  StorageMessageType type;
  Payload payload;

 private:
  explicit StorageMessage(StorageMessageType in_type) : type(in_type) {}

 public:
  ~StorageMessage() {
    // When a union value goes out of the scope, the compiler can't know which
    // destructor to invoke: ensure that a Payload is always cleaned up
    switch (type) {
      case StorageMessageType::TrackingConsentChanged:
        static_assert(
            std::is_trivially_destructible<StorageMessage_TrackingConsentChanged>::value
        );
        break;

      case StorageMessageType::EventGenerated:
        payload.event_generated.~StorageMessage_EventGenerated();
        break;
    }
  }

  // Copying is disallowed; storage queue moves messages in and out
  StorageMessage(const StorageMessage&) = delete;
  StorageMessage& operator=(const StorageMessage&) = delete;

  StorageMessage(StorageMessage&& other) noexcept : type(other.type) {
    // When a union value is moved, the compiler can't know which member's move
    // constructor to invoke: ensure that a Payload is moved correctly based on type,
    // using placement new to construct the new value in-place
    switch (type) {
      case StorageMessageType::TrackingConsentChanged:
        static_assert(
            std::is_trivially_copyable<StorageMessage_TrackingConsentChanged>::value
        );
        payload.tracking_consent_changed = other.payload.tracking_consent_changed;
        break;

      case StorageMessageType::EventGenerated:
        new (&payload.event_generated)
            StorageMessage_EventGenerated(std::move(other.payload.event_generated));
        break;
    }
  }

  StorageMessage& operator=(StorageMessage&& other) noexcept {
    // Handle move-assignment as in the move constructor; cleaning up the destination
    // value first
    if (this != &other) {
      // Destroy the existing payload
      this->~StorageMessage();

      // Move-construct the new payload in-place
      type = other.type;
      switch (type) {
        case StorageMessageType::TrackingConsentChanged:
          static_assert(
              std::is_trivially_copyable<StorageMessage_TrackingConsentChanged>::value
          );
          payload.tracking_consent_changed = other.payload.tracking_consent_changed;
          break;

        case StorageMessageType::EventGenerated:
          new (&payload.event_generated)
              StorageMessage_EventGenerated(std::move(other.payload.event_generated));
          break;
      }
    }
    return *this;
  }

  /**
   * Creates a new TrackingConsentChanged message.
   */
  static StorageMessage TrackingConsentChanged(TrackingConsent value) {
    StorageMessage m{StorageMessageType::TrackingConsentChanged};
    new (&m.payload.tracking_consent_changed)
        StorageMessage_TrackingConsentChanged{value};
    return m;
  }

  /**
   * Creates a new EventGenerated message.
   */
  static StorageMessage EventGenerated(
      FeatureId feature_id, Block event, Block event_metadata
  ) {
    StorageMessage m{StorageMessageType::EventGenerated};
    new (&m.payload.event_generated)
        StorageMessage_EventGenerated{feature_id, event, event_metadata};
    return m;
  }
};

/**
 * Controls how often we create new batch files, based on both time and size limits.
 */
struct BatchWriterConfig {
  /**
   * Once a file exceeds this age, we will not write to it again.
   */
  platform::Duration max_file_age;
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

  explicit BatchWriterConfig(platform::Duration in_max_file_age)
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
      std::unique_ptr<platform::IDirectory>&& directory, const platform::IClock& clock,
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
      platform::Timestamp current_time, Block event, Block event_metadata
  ) const;
  std::optional<std::pair<uint64_t, std::string>> GetFilenameForNextWrite(
      platform::Timestamp current_time
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
      TrackingConsent consent, std::unique_ptr<BatchWriter>&& pending,
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

/**
 * Entry point for the storage thread. See description in `core.hpp`.
 *
 * @param queue Non-owning reference to the thread-safe queue that we should read from;
 *  guaranteed to outlive the thread.
 * @param features Non-owning reference to the array of features that may produce to
 *  that queue. All RegisteredFeature objects contained in the vector are guaranteed to
 *  outlive the thread, and both the objects and the vector itself are guaranteed to
 *  remain immutable for the lifetime of the thread.
 */
void StorageThreadMain(
    Queue<StorageMessage>& queue, std::vector<struct RegisteredFeature>& features
);

}  // namespace datadog::impl
