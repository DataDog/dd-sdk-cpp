// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <atomic>
#include <cinttypes>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include "nonstd/expected.hpp"

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/message_bus.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/queue.hpp"
#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/sdk.hpp"
#include "datadog/impl/core/storage_queue.hpp"
#include "datadog/impl/core/storage_write.hpp"
#include "datadog/impl/core/types.hpp"
#include "datadog/impl/core/upload_scheduler.hpp"
#include "datadog/impl/core/upload_thread.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

// Forward declarations
namespace datadog::platform {
class IClock;
class IHttpSubsystem;
class IHttpClient;
}  // namespace datadog::platform

namespace datadog::impl {

/**
 * Describes the lifecycle of the Core.
 */
enum class CoreState : uint8_t {
  /**
   * The Core has been constructed with its initial CoreConfig, but it has not yet
   * been initialized.
   * - No platform subsystem are initialized.
   * - No features are registered, and no features may be registered.
   * - No threads are running, and the Core may not be started.
   * - Init() must be called (and must return true) to initialize the core.
   */
  Uninitialized,
  /**
   * Init() has been called, but the core is not running:
   * - Platform subsystems (storage, HTTP client, etc.) are fully initialized.
   * - Features may be registered.
   * - No threads are running.
   * - Start() may be called (and must return true) to start the core, once all
   *   desired features are registered. (At least one feature must be registered.)
   */
  Initialized,
  /**
   * Start() has been called: the core is running.
   * - Platform subsystems are fully initialized.
   * - At least one feature is registered; no further features may be registered.
   * - Storage and upload threads are running, and Core state used by those threads
   *   will remain stable until Stop() is called.
   * - Stop() may be called to stop the core, returning it to an Initialized state.
   *   A call to Stop() blocks until the storage queue is drained, pending writes are
   *   flushed to disk, and the storage and reporting threads are shut down cleanly.
   */
  Started,
};

/**
 * Platform-specific dependencies injected when the Core is constructed.
 */
struct CoreSubsystems {
  std::unique_ptr<platform::IClock> clock;
  std::unique_ptr<IFilesystem> fs;
  std::unique_ptr<platform::IHttpSubsystem> http;
  std::unique_ptr<platform::ISystemInfo> system_info;

  explicit CoreSubsystems(
      std::unique_ptr<platform::IClock>&& in_clock,
      std::unique_ptr<IFilesystem>&& in_fs,
      std::unique_ptr<platform::IHttpSubsystem>&& in_http,
      std::unique_ptr<platform::ISystemInfo>&& in_system_info
  )
      : clock(std::move(in_clock)),
        fs(std::move(in_fs)),
        http(std::move(in_http)),
        system_info(std::move(in_system_info)) {}

  /**
   * Initializes the default implementations of platform subsystems, to be injected
   * into the Core.
   *
   * A production build will only contain a single implementation of each subsystem
   * (marked 'final'), each of which implements the static factory function used to
   * create it.
   *
   * Test builds do not use this function; they instead initialize mock
   * implementations.
   */
  static nonstd::expected<CoreSubsystems, ErrorMessage> Init(const CoreConfig& config);
};

/**
 * State maintained by the Core for a feature that's been registered with it.
 */
struct RegisteredFeature {
  /**
   * FourCC ID that uniquely identifies this feature.
   */
  FeatureId id;
  /**
   * Unique name of this feature, used in log messages, directory names, etc.
   */
  std::string name;
  /**
   * Interface used to store this feature's event data on disk, batched into files in
   * preparation for upload.
   */
  std::unique_ptr<FeatureEventStorage> storage;
  /**
   * Pointer to the feature-specific implementation. Uses shared ownership semantics
   * so that the API layer can retain references via std::weak_ptr or std::shared_ptr.
   *
   * All calls to member functions are made by the Core, on the main thread, with one
   * notable exception: the upload thread calls Feature::UploadThread_PrepareReport
   * directly.
   */
  std::shared_ptr<Feature> impl;
  /**
   * Interface used by the storage thread to write event data to persistent storage.
   */
  std::unique_ptr<BatchWriter> batch_writer;
  /**
   * Stores feature-specific timing details and other state information, for use by
   * the upload thread.
   */
  std::unique_ptr<UploadThreadState> upload_state;

  explicit RegisteredFeature(
      FeatureId in_id,
      std::string_view in_name,
      std::unique_ptr<FeatureEventStorage>&& in_storage,
      const std::shared_ptr<Feature>& in_impl,
      std::unique_ptr<BatchWriter>&& in_batch_writer,
      std::unique_ptr<UploadThreadState>&& in_upload_state
  )
      : id(in_id),
        name(in_name),
        storage(std::move(in_storage)),
        impl(in_impl),
        batch_writer(std::move(in_batch_writer)),
        upload_state(std::move(in_upload_state)) {}
};

/**
 * Implements the core business logic of the Datadog SDK; namely:
 *
 * - allowing modular features to generate event data in response to API operations
 * - quickly flushing that event data to persistent storage in a background thread
 * - periodically processing and uploading batches of event data in another thread
 *
 * The entry point to the C API is a series of functions that operate on dd_core_t, e.g.
 * dd_core_init(). The entry point to the C++ API is the datadog::Core type.
 * datadog::impl::Core handles API calls from both of those interfaces.
 *
 * The Core owns all global resources that belong to a specific instance of the SDK.
 *
 * On Init(), the Core initializes platform-specific subsystems that include persistent
 * filesystem storage, HTTP client functionality, etc.
 *
 * The Core manages a set of Feature implementations (see `feature.hpp`) that have been
 * registered with it. Each Feature provides its own set of user-callable operations,
 * and each Feature knows how to do two things that are of concern to the Core:
 *
 * 1.) A feature can generate event payloads in response to API operations, yielding
 *     data that can be flushed to persistent storage in batches
 *
 * 2.) A feature can process batches of those same payloads when they are ready for
 *     upload, describing an appropriate HTTP request that will send them to intake
 *
 * RegisterFeature() may be called (after Init() but before Start()) in order to
 * activate a specific subset of SDK functionality. Once the core is started, all
 * registered feature implementations receive OnStart(). When the core is stopped, all
 * features receive OnStop() prior to shutdown.
 *
 * When started, the Core runs two background threads:
 *
 * 1.) The storage thread (see `storage_thread.hpp`), which consumes from a thread-safe
 *     queue that's owned by the Core (see `queue.hpp`). When a feature generates new
 *     event data, or when the Core needs to change the state of the storage thread,
 *     code running in the the main thread produces a `StorageMessage` to the storage
 *     queue.
 *
 *     For each registered feature, the storage thread takes responsibility for writing
 *     event data to '<storage_root>/<feature_name>/', split into two subdirectories:
 *     one for batches of events collected while tracking consent is pending, and
 *     another containing batches that we have consent to upload. Each batch is a stored
 *     as a binary file in TLV format (see `tlv.hpp`).
 *
 *     In `StorageThreadMain`, the storage thread consumes from the storage queue,
 *     blocking until messages are available, and processes each message serially.
 *
 *     When a new event has been queued for write, the storage thread writes it to the
 *     appropriate batch file, in '<storage_root>/<feature_name>/<consent_subdir>/'.
 *     The name of each batch file is a Unix timestamp (in milliseconds, no extension)
 *     representing the timestamp at which it was created. The storage thread makes
 *     decisions about when to stop writing to the the current file and start a new
 *     batch, based on size limits, timing requirements, etc., some of which are
 *     user-configurable and some of which are feature-defined.
 *
 *     When the storage thread is stopped in response to Core::Stop():
 *     - The storage queue will stop accepting new messages
 *     - The storage thread will drain the queue, flushing all pending writes to disk
 *     - The storage thread will exit
 *
 * 2.) The upload thread (see `upload_thread.hpp`), which uses an `UploadScheduler` to
 *     periodically initiate "upload cycles" for all registered features. The timing of
 *     upload cycles is controlled independently for each feature, but the upload thread
 *     only handles a single upload cycle at a time.
 *
 *     For each registered feature, the upload thread takes responsibility for reading
 *     event data from '<storage_root>/<feature_name>/<consent_subdir>', where
 *     consent_subdir is the subdirectory that contains event data that we have consent
 *     to process and upload.
 *
 *     In `UploadThreadMain`, the upload thread waits until the next scheduled upload
 *     cycle is ready. At that time, it runs an upload cycle for the relevant feature,
 *     then schedules the next upload cycle for the same feature, adjusting the delay
 *     for that feature as needed based on the results of the cycle it just ran.
 *
 *     In each upload cycle, the upload thread scans through the target directory in
 *     order, starting from the oldest file, looking for batches of event data that are
 *     ready to be processed and uploaded. Files are not considered ready for upload
 *     until they've reached a certain age, and the storage thread refrains from writing
 *     to files once they're nearing that age, providing a time-based means of
 *     synchronizing access to the filesystem.
 *
 *     Each upload cycle may process multiple batches for a given feature. For each
 *     batch, the upload thread opens the relevant file for read, then defers to the
 *     feature implementation to read each event from the batch, along with any
 *     associated metadata, in order to build an HTTP request payload. The upload thread
 *     initiates an HTTP request as instructed by the feature implementation, streaming
 *     the request body directly over the HTTP connection.
 *
 *     When the upload thread successfully processes a batch, or when it determines that
 *     the batch is malformed or unacceptable to the intake server, it deletes the batch
 *     from the storage directory. If the upload thread fails to upload a batch due to
 *     transient network problems, it retains the batch and aborts the upload cycle.
 *
 *     When the upload thread successfully uploads batches for a feature, it reduces the
 *     interval between upload cycles for that feature. When upload cycles fail, the
 *     storage thread increases the delay with which that feature's next upload cycle is
 *     scheduled.
 *
 *     The upload thread makes decisions about the timing and throughput of upload
 *     cycles based on user-configurable values.
 *
 *     When the upload thread is stopped in response to Core::Stop():
 *     - If there is an upload cycle in progress, it will be completed
 *     - The upload thread will exit
 *
 * All state shared between threads is owned by the core, whose lifetime exceeds that of
 * the threads. Aside from the storage queue and the upload scheduler, which are
 * thread-safe by design, the primary point of overlap between threads is the vector of
 * RegisteredFeature objects that is maintained by the Core and shared by reference
 * between threads.
 *
 * The Core guarantees that this vector and all its RegisteredFeature objects will
 * remain immutable for the lifetime of all threads, and each thread treats these
 * state objects as read-only, obviating the need for synchronization. The storage
 * thread has exclusive access to a feature's `BatchWriter` interface, and the upload
 * thread has exclusive access to a feature's `UploadThreadState`.
 */
class Core {
 public:
  /**
   * Constructs a new core from the provided configuration.
   */
  explicit Core(const CoreConfig& config, CoreSubsystems&& subsystems);

  /**
   * Ensures that the core is stopped, if necessary, when it goes out of scope.
   */
  ~Core();

  // Core is not copyable, not movable
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;
  Core(Core&&) noexcept = delete;
  Core& operator=(Core&&) noexcept = delete;

  /**
   * Updates the configured tracking consent value.
   *
   * May be called at any time from the main thread. If the core is running, the state
   * change will be pushed to the storage thread.
   */
  void SetTrackingConsent(TrackingConsent value);

  /**
   * Sets user info in the global context. The `extra` attribute must be an object type;
   * any other type is ignored. Propagated to RUM `usr` and log `usr` fields.
   */
  void SetUserInfo(
      std::string_view id,
      std::optional<std::string_view> name,
      std::optional<std::string_view> email,
      const Attribute& extra
  );

  /**
   * Merges additional key-value pairs into the existing user info extra attributes.
   * If no user info is set yet, creates a new user info entry with only extra set.
   * The `extra` attribute must be an object type; any other type is ignored.
   */
  void AddUserExtraInfo(const Attribute& extra);

  /**
   * Clears all user info from the global context.
   */
  void ClearUserInfo();

  /**
   * Initializes the core.
   *
   * Must be called before RegisterFeature() or Start() may be called. May not be called
   * more than once.
   */
  bool Init();

  /**
   * Prepares a storage directory within <application-storage>/.datadog/ with the given
   * name. Name must be dot-prefixed. Returns null if the directory could not be
   * created.
   */
  std::unique_ptr<ArtifactStorage> InitializeArtifactStorage(
      std::string_view directory_name
  );

  /**
   * Registers a feature implementation with the core.
   *
   * Must be called after Init() but before Start(): features may not be registered
   * while the core is running. No feature can be registered more than once. Once
   * registered, a feature can not be unregistered.
   *
   * @returns whether the feature was successfully registered. If the feature was
   *  already registered, returns false nonetheless.
   */
  bool RegisterFeature(const std::shared_ptr<Feature>& impl);

  /**
   * Starts the core.
   *
   * Must be called after Init(), and after at least one feature has been successfully
   * registered. The core must not already be running.
   *
   * If successful, starts the storage and upload threads, notifies all registered
   * features, and begins normal SDK operation.
   *
   * @returns whether the core was successfully started. If the core was already
   *  running, returns false nonetheless.
   */
  bool Start();

  /**
   * Stops the core, ensuring that all background threads are stopped and all in-flight
   * processing is completed cleanly.
   *
   * If the core is not actually running, including if the core is not initialized, has
   * no effect.
   *
   * If the core is running: notifies all features; then signals the end of storage
   * queue processing; waits for the storage thread to drain the queue and flush writes
   * to disk; then signals the end of upload cycle scheduling and waits for the upload
   * thread to complete any in-flight HTTP request and exit.
   */
  void Stop();

 private:
  bool EnqueueStorageWrite(FeatureId feature_id, Block event, Block event_metadata);

 private:
  // Initialized in ctor
  CoreState _state{CoreState::Uninitialized};
  CoreConfig _config;
  DiagnosticLogger _diagnostic_logger;
  std::unique_ptr<CoreContextProvider> _context_provider;
  CoreSubsystems _subsystems;

  // Initialized on Init, before features can be registered
  std::unique_ptr<platform::IHttpClient> _http_client;
  std::optional<SdkStorage> _storage;

  // Initialized before Start in response to user-initiated feature registration
  std::vector<RegisteredFeature> _features;  // May not be modified while running

  // Initialized on Start, cleaned up on Stop
  std::unique_ptr<StorageQueue> _storage_queue;
  std::optional<std::thread> _storage_thread;

  std::unique_ptr<Queue<std::function<void()>>> _context_queue;
  std::optional<std::thread> _context_thread;

  std::unique_ptr<MessageBus> _message_bus;
  std::optional<std::thread> _message_bus_thread;

  std::unique_ptr<UploadScheduler> _upload_scheduler;
  std::optional<std::thread> _upload_thread;

 public:
  // Accessors for core-owned data and interfaces that need to be passed to feature
  // implementation when they're constructed
  const platform::IClock& GetClock() const;
  IFilesystem& GetFilesystem() const;
  std::string_view GetServiceName() const;
  std::string_view GetApplicationVersion() const;
};

}  // namespace datadog::impl
