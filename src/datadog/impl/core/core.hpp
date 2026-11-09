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
#include <vector>

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/executor.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/message_bus.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/sdk.hpp"
#include "datadog/impl/core/storage_queue.hpp"
#include "datadog/impl/core/storage_write.hpp"
#include "datadog/impl/core/types.hpp"
#include "datadog/impl/core/upload_thread.hpp"
#include "datadog/impl/types/diagnostics.hpp"

// Forward declarations
namespace datadog::platform {
class IClock;
}  // namespace datadog::platform

namespace datadog::impl {

// Forward declarations
class IHttpSubsystem;
class IHttpClient;

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
  std::unique_ptr<IHttpSubsystem> http;
  std::unique_ptr<platform::ISystemInfo> system_info;

  explicit CoreSubsystems(
      std::unique_ptr<platform::IClock>&& in_clock,
      std::unique_ptr<IFilesystem>&& in_fs,
      std::unique_ptr<IHttpSubsystem>&& in_http,
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
  static std::optional<CoreSubsystems> Init(const DiagnosticLogger& logger);
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
 * When started, the Core runs its background work on an `Executor` (see
 * `executor.hpp`): a set of worker threads, capped by
 * `CoreConfig::SetMaxWorkerThreads`, shared by several serial queues. Each queue runs
 * its items one at a time, in FIFO order, on whichever worker is free, so the work on
 * each queue is serialized exactly as if it had a dedicated thread. By default the
 * executor may spawn one worker per queue, so no queue ever waits for another. The Core
 * owns four queues:
 *
 * 1.) The context queue, a TaskQueue of functions submitted by features through
 *     FeatureScope (see `feature_scope.hpp`). Features rely on these functions never
 *     running concurrently with each other.
 *
 * 2.) The message bus queue (see `message_bus.hpp`), which delivers FeatureMessages to
 *     every feature's handler. Delivery starts only once every feature has received
 *     OnCoreStarted().
 *
 * 3.) The storage queue (see `storage_thread.hpp`). When a feature generates new event
 *     data, or when the Core needs to change the state of storage, a `StorageMessage`
 *     is pushed onto this queue, and `HandleStorageMessage` processes each one
 *     serially.
 *
 *     For each registered feature, the storage queue takes responsibility for writing
 *     event data to '<storage_root>/<feature_name>/', split into two subdirectories:
 *     one for batches of events collected while tracking consent is pending, and
 *     another containing batches that we have consent to upload. Each batch is a stored
 *     as a binary file in TLV format (see `tlv.hpp`).
 *
 *     When a new event has been queued for write, it is written to the appropriate
 *     batch file, in '<storage_root>/<feature_name>/<consent_subdir>/'. The name of
 *     each batch file is a Unix timestamp (in milliseconds, no extension) representing
 *     the timestamp at which it was created. Storage makes decisions about when to stop
 *     writing to the the current file and start a new batch, based on size limits,
 *     timing requirements, etc., some of which are user-configurable and some of which
 *     are feature-defined.
 *
 * 4.) The upload queue (see `upload_thread.hpp`), on which an `UploadWorker`
 *     periodically runs "upload cycles" for all registered features. The timing of
 *     upload cycles is controlled independently for each feature, but only a single
 *     upload cycle runs at a time, and the HTTP client is only ever used for one
 *     request at a time.
 *
 *     For each registered feature, the upload queue takes responsibility for reading
 *     event data from '<storage_root>/<feature_name>/<consent_subdir>', where
 *     consent_subdir is the subdirectory that contains event data that we have consent
 *     to process and upload.
 *
 *     When a feature's upload cycle is due, the UploadWorker runs it, then schedules
 *     the next upload cycle for the same feature, adjusting the delay for that feature
 *     as needed based on the results of the cycle it just ran.
 *
 *     In each upload cycle, the upload queue scans through the target directory in
 *     order, starting from the oldest file, looking for batches of event data that are
 *     ready to be processed and uploaded. Files are not considered ready for upload
 *     until they've reached a certain age, and storage refrains from writing to files
 *     once they're nearing that age, providing a time-based means of synchronizing
 *     access to the filesystem.
 *
 *     Each upload cycle may process multiple batches for a given feature, each in its
 *     own queue item. For each batch, the upload queue opens the relevant file for
 *     read, then defers to the feature implementation to read each event from the
 *     batch, along with any associated metadata, in order to build an HTTP request
 *     payload. It then initiates an HTTP request as instructed by the feature
 *     implementation, streaming the request body directly over the HTTP connection.
 *
 *     When a batch is processed successfully, or when it is malformed or unacceptable
 * to the intake server, it is deleted from the storage directory. If a batch fails to
 *     upload due to transient network problems, it is retained and the upload cycle is
 *     aborted.
 *
 *     When uploads for a feature succeed, the interval between upload cycles for that
 *     feature is reduced. When upload cycles fail, the delay with which that feature's
 *     next upload cycle is scheduled is increased, based on user-configurable values.
 *
 * Features may create further queues on the same executor through FeatureScope (e.g.
 * RUM's vitals sampling), and own them.
 *
 * Each queue has a stop policy (see `StopPolicy`). Core::Stop() first cancels all
 * skippable work (the upload queue and feature queues such as vitals sampling), then
 * drains the context, message bus and storage queues in that order, since each feeds
 * the next. The only upload work it waits for is an HTTP request already in flight.
 *
 * All state shared between queues is owned by the core, whose lifetime exceeds that of
 * the executor. Aside from the queues themselves, which are thread-safe by design, the
 * primary point of overlap between queues is the vector of RegisteredFeature objects
 * that is maintained by the Core and shared by reference.
 *
 * The Core guarantees that this vector and all its RegisteredFeature objects will
 * remain immutable while the executor is running, and each queue treats these state
 * objects as read-only, obviating the need for synchronization. The storage queue has
 * exclusive access to a feature's `BatchWriter` interface, and the upload queue has
 * exclusive access to a feature's `UploadThreadState`.
 */
class Core {
 public:
  /**
   * Constructs a new core from the provided configuration.
   */
  explicit Core(
      const CoreConfig& config,
      TrackingConsent tracking_consent,
      CoreSubsystems&& subsystems
  );

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
   * Mutates `CoreContext::user_info` to match the provided values.
   */
  void SetUserInfo(
      std::string_view id,
      std::string_view name,
      std::string_view email,
      const Attribute& extra
  );

  /**
   * Mutates `CoreContext::user_info` to merge the provided values into any existing
   * extra attributes. If `extra` is not an Object with 1 or more properties, has no
   * effect.
   */
  void AddUserExtraInfo(const Attribute& extra);

  /**
   * Mutates `CoreContext::user_info`, resetting it to a default (empty) state.
   */
  void ClearUserInfo();

  /**
   * Mutates `CoreContext::account_info` to match the provided values.
   */
  void SetAccountInfo(
      std::string_view id, std::string_view name, const Attribute& extra
  );

  /**
   * Mutates `CoreContext::account_info` to merge the provided values into any existing
   * extra attributes. If `extra` is not an Object with 1 or more properties, has no
   * effect.
   */
  void AddAccountExtraInfo(const Attribute& extra);

  /**
   * Mutates `CoreContext::account_info`, resetting it to a default (empty) state.
   */
  void ClearAccountInfo();

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
   * If successful, starts the executor and its queues, notifies all registered
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
   * If the core is running: cancels pending upload cycles and other skippable work;
   * drains the context and message bus queues; notifies all features; drains the
   * storage queue, flushing writes to disk; waits for any in-flight HTTP request to
   * complete; then joins all worker threads.
   *
   * Must not be called from one of the SDK's worker threads.
   */
  void Stop();

 private:
  bool EnqueueStorageWrite(
      FeatureId feature_id,
      Block event,
      Block event_metadata,
      bool bypass_tracking_consent
  );
  void UpdateContext(const std::function<void(CoreContext&)>& callback);

 private:
  // Initialized in ctor
  CoreState _state{CoreState::Uninitialized};
  CoreConfig _config;
  TrackingConsent _tracking_consent;
  ImmutableContext _immutable_context;
  DiagnosticLogger _diagnostic_logger;
  std::unique_ptr<CoreContextProvider> _context_provider;
  CoreSubsystems _subsystems;

  // Initialized on Init, before features can be registered
  std::unique_ptr<IHttpClient> _http_client;
  std::optional<SdkStorage> _storage;

  // Initialized before Start in response to user-initiated feature registration
  std::vector<RegisteredFeature> _features;  // May not be modified while running

  // Initialized on Start, cleaned up on Stop. The executor is declared first so that
  // it outlives the queues created from it.
  std::unique_ptr<Executor> _executor;
  std::unique_ptr<StorageQueue> _storage_queue;
  std::unique_ptr<TaskQueue> _context_queue;
  std::unique_ptr<MessageBus> _message_bus;
  std::unique_ptr<UploadWorker> _upload_worker;

 public:
  // Accessors for core-owned data and interfaces that need to be passed to feature
  // implementation when they're constructed
  const platform::IClock& GetClock() const;
  IFilesystem& GetFilesystem() const;
  std::string_view GetIntakeOrigin() const;
  std::string_view GetClientToken() const;
};

}  // namespace datadog::impl
