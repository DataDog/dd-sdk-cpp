// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/core.hpp"

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/core/context_thread.hpp"
#include "datadog/impl/core/storage_thread.hpp"
#include "datadog/impl/core/types.hpp"
#include "datadog/impl/core/version.hpp"
#include "datadog/impl/platform/clock.hpp"
#include "datadog/impl/platform/filesystem.hpp"
#include "datadog/impl/platform/http.hpp"
#include "datadog/impl/platform/system_info.hpp"

namespace datadog::impl {

nonstd::expected<CoreSubsystems, ErrorMessage> CoreSubsystems::Init(
    const CoreConfig& config
) {
  // Prepare a wrapper object that can read from the system clock
  auto clock = platform::Clock::Init();
  if (!clock) {
    return nonstd::make_unexpected(
        ErrorMessage("clock subsystem could not be initialized")
    );
  }

  std::filesystem::path storage_root_path = ".datadog";
  if (!config.event_storage_location.empty()) {
    storage_root_path =
        std::filesystem::path(config.event_storage_location) / ".datadog";
  } else {
    impl::DiagnosticLogger{config.diagnostic_handler, config.diagnostic_threshold}
        .Warning(
            "Events will be stored within .datadog/ in the current working directory: "
            "application should call SetEventStorageLocation to specify a suitable "
            "application-specific directory where .datadog/ can be created"
        );
  }

  // Initialize filesystem storage, creating a Datadog-SDK-managed subdirectory beneath
  // the configured path
  auto filesystem_result = platform::Filesystem::Init(storage_root_path.string());
  if (!filesystem_result) {
    return nonstd::make_unexpected(filesystem_result.error().AddPrefix(
        "event storage subsystem could not be initialized"
    ));
  }
  auto storage_root = std::move(*filesystem_result);

  // Prepare whatever HTTP client library we'll use to create HTTP clients
  auto http_result = platform::Http::Init();
  if (!http_result) {
    return nonstd::make_unexpected(
        http_result.error().AddPrefix("HTTP subsystem could not be initialized")
    );
  }
  auto http = std::move(*http_result);

  // Initialize system information collection
  impl::DiagnosticLogger diagnostic_logger{
      config.diagnostic_handler, config.diagnostic_threshold
  };
  auto system_info = platform::SystemInfo::Init(diagnostic_logger);

  // Return our newly-created subsystems, to be transferred into the Core
  return CoreSubsystems(
      std::move(clock), std::move(storage_root), std::move(http), std::move(system_info)
  );
}

Core::Core(const CoreConfig& config, CoreSubsystems&& subsystems)
    : _config(config),
      _diagnostic_logger(config.diagnostic_handler, config.diagnostic_threshold),
      _context_provider(
          std::make_unique<CoreContextProvider>(CoreContext(
              config,
              subsystems.system_info->GetOsInfo(),
              subsystems.system_info->GetDeviceInfo()
          ))
      ),
      _subsystems(std::move(subsystems)) {
  DATADOG_ASSERT(
      _subsystems.storage_root, "Core created with no root storage directory"
  );
  DATADOG_ASSERT(_subsystems.http, "Core created with no HTTP subsystem");
  DATADOG_ASSERT(_subsystems.system_info, "Core created with no system info subsystem");

  _features.reserve(16);
}

Core::~Core() { Stop(); }

void Core::SetTrackingConsent(TrackingConsent value) {
  if (_config.tracking_consent != value) {
    // Store the updated consent value, which we use to initialize storage-thread state
    // on core start
    _config.tracking_consent = value;

    // If we're already started, send a message to the storage thread so it can handle
    // the state change
    if (_state == CoreState::Started) {
      // If the core is running, send a message using the storage queue
      DATADOG_ASSERT(
          _storage_queue,
          "_storage_queue is invalid with CoreState::Started on "
          "SetTrackingConsent"
      );
      _storage_queue->Push(StorageMessage::TrackingConsentChanged(value));
    } else {
      // Otherwise, notify each feature directly, since BatchWriter is initialized from
      // config on RegisterFeature(), not on Start()
      for (auto& feature : _features) {
        if (!feature.batch_writer->SetTrackingConsent(value)) {
          _diagnostic_logger.Warning("Failed to set tracking consent synchronously");
        }
      }
    }
  }
}

bool Core::Init() {
  // We call Init() internally at the API binding layer; users should not be able to
  // attempt initialization of the same core twice
  DATADOG_ASSERT(
      _state == CoreState::Uninitialized, "Core::Init called multiple times"
  );

  // Create a single HTTP client
  _http_client = _subsystems.http->CreateClient();
  if (!_http_client) {
    _diagnostic_logger.Error(
        "Core initialization failed: could not create HTTP client"
    );
    return false;
  }

  // Core is initialized; ready to register features and start
  _state = CoreState::Initialized;
  _diagnostic_logger.Debug("Core initialization complete");
  return true;
}

bool Core::RegisterFeature(const std::shared_ptr<Feature>& impl) {
  const FeatureId id = impl->GetId();
  const std::string_view name = impl->GetName();
  // Features may only be registered after init but before the core is started
  if (_state != CoreState::Initialized) {
    if (_state == CoreState::Started) {
      _diagnostic_logger.Warning(
          "Ignoring request to register feature: Core is already running",
          {{"feature", name}, {"feature_id", static_cast<int64_t>(id)}}
      );
    } else {
      _diagnostic_logger.Warning(
          "Ignoring request to register feature: Core is uninitialized",
          {{"feature", name}, {"feature_id", static_cast<int64_t>(id)}}
      );
    }
    return false;
  }

  // Don't allow a feature to be registered with a duplicate ID (each feature must have
  // a unique ID, and each feature may only be registered once), and don't alllow two
  // features to have the same name, either, as this would cause filesystem contention
  const auto existing =
      std::find_if(_features.begin(), _features.end(), [&](const RegisteredFeature& f) {
        return f.id == id || f.name == name;
      });
  if (existing != _features.end()) {
    _diagnostic_logger.Warning(
        "Ignoring request to register feature: a feature is already registered with "
        "that name or id",
        {{"feature", name}, {"feature_id", static_cast<int64_t>(id)}}
    );
    return false;
  }

  // Initialize a subdirectory within our root storage directory that will contain files
  // written on behalf of this feature
  auto feature_subdir = _subsystems.storage_root->PrepareSubdirectory(name);
  if (!feature_subdir) {
    _diagnostic_logger.Error(
        "Failed to register feature: storage directory could not be initialized",
        {{"feature", name},
         {"feature_id", static_cast<int64_t>(id)},
         {"fs_error_type", static_cast<int64_t>(feature_subdir.error())}}
    );
    return false;
  }

  // Initialize two subdirectories within that feature directory: one that we'll write
  // to when tracking consent is pending, and another to contain the files that the user
  // has consented to being uploaded: the upload thread will read from the latter
  auto pending_subdir =
      (*feature_subdir)->PrepareSubdirectory(BatchWriter::PENDING_SUBDIRECTORY_NAME);
  if (!pending_subdir) {
    _diagnostic_logger.Error(
        "Failed to register feature: storage subdirectory could not be initialized",
        {{"feature", name},
         {"feature_id", static_cast<int64_t>(id)},
         {"subdir_name", BatchWriter::PENDING_SUBDIRECTORY_NAME},
         {"fs_error_type", static_cast<int64_t>(feature_subdir.error())}}
    );
    return false;
  }
  auto granted_subdir =
      (*feature_subdir)->PrepareSubdirectory(BatchWriter::GRANTED_SUBDIRECTORY_NAME);
  if (!granted_subdir) {
    _diagnostic_logger.Error(
        "Failed to register feature: storage subdirectory could not be initialized",
        {{"feature", name},
         {"feature_id", static_cast<int64_t>(id)},
         {"subdir_name", BatchWriter::GRANTED_SUBDIRECTORY_NAME},
         {"fs_error_type", static_cast<int64_t>(feature_subdir.error())}}
    );
    return false;
  }

  // Initialize the BatchWriter object that the storage thread will use to persist
  // events to disk as they're generated by this feature implementation
  auto writer_config = BatchWriterConfig::FromBatchSize(_config.batch_size);
  auto batch_writer = std::make_unique<BatchWriter>(
      _diagnostic_logger,
      _config.tracking_consent,
      std::move(*pending_subdir),
      std::move(*granted_subdir),
      *_subsystems.clock,
      writer_config
  );

  // Prepare a separate interface to the directory that the upload thread should read
  // from: this is the same location on disk that the storage thread may write to (i.e.
  // granted_subdir), but we want each thread to have its own handle
  auto event_read_directory =
      (*feature_subdir)->PrepareSubdirectory(BatchWriter::GRANTED_SUBDIRECTORY_NAME);
  if (!event_read_directory) {
    _diagnostic_logger.Error(
        "Failed to register feature: upload subdirectory could not be initialized",
        {{"feature", name},
         {"feature_id", static_cast<int64_t>(id)},
         {"subdir_name", BatchWriter::GRANTED_SUBDIRECTORY_NAME},
         {"fs_error_type", static_cast<int64_t>(feature_subdir.error())}}
    );
    return false;
  }

  // Initialize the feature-specific state used by the upload thread
  auto upload_state = std::make_unique<UploadThreadState>(_config.upload_frequency);

  _features.emplace_back(
      id,
      name,
      impl,
      std::move(*feature_subdir),
      std::move(batch_writer),
      std::move(*event_read_directory),
      std::move(upload_state)
  );
  _diagnostic_logger.Debug(
      "Feature registered",
      {{"feature", name}, {"feature_id", static_cast<int64_t>(id)}}
  );
  return true;
}

bool Core::Start() {
  // Start() may only be called after Init(), and while the core is not yet started
  if (_state != CoreState::Initialized) {
    if (_state == CoreState::Started) {
      _diagnostic_logger.Warning(
          "Ignoring request to start Core: Core is already running"
      );
    } else {
      _diagnostic_logger.Warning(
          "Ignoring request to start Core: Core is uninitialized"
      );
    }
    return false;
  }

  _diagnostic_logger.Debug("Beginning Core startup");

  // At least one feature must have been registered
  if (_features.empty()) {
    _diagnostic_logger.Error(
        "Failed to start SDK: application must successfully register at least one "
        "feature"
    );
    return false;
  }

  // Initialize a thread-safe queue that features can write to whenever they produce
  // events that need to be written to disk
  DATADOG_ASSERT(!_storage_queue, "_storage_queue already exists on Start()");
  _storage_queue = std::make_unique<StorageQueue>();

  // Start a thread that will read those events from the queue and write them to
  // persistent storage: the thread accepts non-owning references to the queue and the
  // vector of features, as both are stable for the lifetime of the thread
  DATADOG_ASSERT(!_storage_thread, "_storage_thread already exists on Start()");
  _storage_thread = std::thread(
      StorageThreadMain,
      std::ref(_diagnostic_logger),
      std::ref(*_storage_queue),
      std::ref(_features)
  );

  // Initialize a thread-safe queue for functions that will execute on the context
  // thread
  DATADOG_ASSERT(!_context_queue, "_context_queue already exists on Start()");
  _context_queue = std::make_unique<Queue<std::function<void()>>>();

  // Start the context thread that will execute functions submitted by features
  DATADOG_ASSERT(!_context_thread, "_context_thread already exists on Start()");
  _context_thread = std::thread(
      ContextThreadMain,
      std::ref(_diagnostic_logger),
      std::ref(*_context_queue),
      std::ref(*_context_provider)
  );

  // Initialize an upload scheduler to manage the timing of upload cycles for each
  // feature
  DATADOG_ASSERT(!_upload_scheduler, "_upload_scheduler already exists on Start()");
  _upload_scheduler = std::make_unique<UploadScheduler>(*_subsystems.clock);

  // Acquire a reference to the HttpContext that's held within our CoreContext: the
  // HttpContext is immutable, so it's safe for the upload thread to retain this
  // reference
  DATADOG_ASSERT(_context_provider, "_context_provider is null on Start()");
  const HttpContext& http_context = _context_provider->GetHttpContext();

  // Start another thread that will schedule periodic upload cycles on a per-feature
  // basis: each time an upload cycle runs, the thread will check the relevant storage
  // directory for batches of events that are ready for read, processing them via the
  // feature implementation and sending them to intake over HTTP
  DATADOG_ASSERT(!_upload_thread, "_upload_thread already exists on Start()");
  _upload_thread = std::thread(
      UploadThreadMain,
      std::ref(_diagnostic_logger),
      UploadThreadConfig::FromCoreConfig(
          _config.batch_size, _config.batch_processing_level
      ),
      std::ref(http_context),
      std::ref(*_subsystems.clock),
      std::ref(*_upload_scheduler),
      std::ref(_features),
      std::ref(*_http_client)
  );

  _diagnostic_logger.Status(
      "Core started",
      {{"service", _config.service},
       {"start_time", _subsystems.clock->Now()},
       {"env", _config.env},
       {"application_version", _config.application_version},
       {"sdk_version", SDK_VERSION}}
  );
  _state = CoreState::Started;

  // Notify each registered feature that the core has started, providing it with a
  // FeatureScope interface that it can use to interoperate with the core
  for (const auto& feature : _features) {
    const FeatureId id = feature.id;
    EventGeneratedFunc event_generated_func =
        [this, id](Block event, Block event_metadata) -> bool {
      return EnqueueStorageWrite(id, event, event_metadata);
    };
    feature.impl->OnCoreStarted(
        FeatureScope::Create(
            *_context_provider,
            event_generated_func,
            _diagnostic_logger,
            *_context_queue
        )
    );
  }
  return true;
}

void Core::Stop() {
  // Double-shutdown is fine; just ignore it
  if (_state != CoreState::Started) {
    return;
  }
  _diagnostic_logger.Debug("Beginning Core shutdown");

  // Notify each registered feature that the core has stopped
  for (const auto& feature : _features) {
    feature.impl->OnCoreStopping();
  }

  // If we were previously started, all background threads should be running
  DATADOG_ASSERT(_context_queue, "_context_queue is invalid on Stop");
  DATADOG_ASSERT(
      _context_thread && _context_thread->joinable(),
      "_context_thread is non-joinable on Stop"
  );
  DATADOG_ASSERT(_storage_queue, "_storage_queue is invalid on Stop");
  DATADOG_ASSERT(
      _storage_thread && _storage_thread->joinable(),
      "_storage_thread is non-joinable on Stop"
  );
  DATADOG_ASSERT(_upload_scheduler, "_upload_scheduler is invalid on Stop");
  DATADOG_ASSERT(
      _upload_thread && _upload_thread->joinable(),
      "_upload_thread is non-joinable on Stop"
  );

  // Stop the context queue, then block until the context thread drains the queue and
  // exits. This ensures all pending feature work completes before we stop the storage
  // thread.
  _context_queue->Stop();
  if (_context_thread) {
    _diagnostic_logger.Debug("Joining on context thread");
    _context_thread->join();
  }
  _context_thread.reset();
  _context_queue.reset();

  // Stop all queue processing, then block until the consumer thread drains the queue
  // and exits, at which point it's safe to release the queue
  _storage_queue->Stop();
  if (_storage_thread) {
    _diagnostic_logger.Debug("Joining on storage thread");
    _storage_thread->join();
  }
  _storage_thread.reset();
  _storage_queue.reset();

  // Once the storage thread is fully shut down, signal to the upload thread (with
  // synchronization) that it should exit, then wait for it to do so
  _upload_scheduler->Stop();
  if (_upload_thread) {
    _diagnostic_logger.Debug("Joining on upload thread");
    _upload_thread->join();
  }
  _upload_thread.reset();
  _upload_scheduler.reset();

  // If we're configured to flush at shutdown, run one final upload cycle for each
  // registered feature, attempting to upload the next N batches, blocking the main
  // thread until done: this is useful for ensuring immediate uploads in unit tests
  if (_config.internal_options.num_http_requests_per_feature_to_flush_on_stop > 0) {
    // Adjust our config to allow reading all files, regardless of age (since we've
    // joined on the storage thread; we now have exclusive access to batch files), and
    // to upload N batches up to our configured per-feature limit
    Duration min_file_age_for_read{0};
    UploadThreadConfig flush_config(
        min_file_age_for_read,
        _config.internal_options.num_http_requests_per_feature_to_flush_on_stop
    );

    // We can't reuse buffers from the upload thread (unless we factor them out and make
    // them owned by the core)
    std::vector<std::string> mut_filenames;
    std::vector<char> mut_read_buffer;

    // Get the HTTP context for use in the upload thread routine
    DATADOG_ASSERT(_context_provider, "_context_provider is null on Stop()");
    const HttpContext& http_context = _context_provider->GetHttpContext();

    // Run our upload cycle procedure on the main thread, synchronously: now that
    // we've joined on both threads, all state is synchronized
    for (const auto& feature : _features) {
      Internal_HandleUploadProc(
          _diagnostic_logger,
          flush_config,
          http_context,
          *_subsystems.clock,
          feature.id,
          _features,
          *_http_client,
          mut_filenames,
          mut_read_buffer
      );
    }
  }

  _diagnostic_logger.Status("Core stopped", {{"stop_time", _subsystems.clock->Now()}});

  // Revert to the initialized state; subsequent calls to Start() will restart us
  _state = CoreState::Initialized;
}

bool Core::EnqueueStorageWrite(
    FeatureId feature_id, Block event, Block event_metadata
) {
  if (_state != CoreState::Started) {
    _diagnostic_logger.Warning(
        "Ignoring event generated while Core not running",
        {{"feature_id", static_cast<int64_t>(feature_id)}}
    );
    return false;
  }

  DATADOG_ASSERT(_storage_queue, "_storage_queue is invalid while core is running");
  return _storage_queue->Push(
      StorageMessage::EventGenerated(feature_id, event, event_metadata)
  );
}

const platform::IClock& Core::GetClock() const {
  DATADOG_ASSERT(_state >= CoreState::Initialized, "GetClock called before Core init");
  DATADOG_ASSERT(_subsystems.clock, "Clock not present after Core init");
  return *_subsystems.clock;
}

std::string_view Core::GetServiceName() const {
  DATADOG_ASSERT(
      _state >= CoreState::Initialized, "GetServiceName called before Core init"
  );
  return _context_provider->GetHttpContext().service;
}

std::string_view Core::GetApplicationVersion() const {
  DATADOG_ASSERT(
      _state >= CoreState::Initialized, "GetApplicationVersion called before Core init"
  );
  return _context_provider->GetHttpContext().application_version;
}

void Core::FlushContextQueue() {
  DATADOG_ASSERT(
      _state == CoreState::Started, "FlushContextQueue called while Core not running"
  );
  DATADOG_ASSERT(_context_queue, "_context_queue is null on FlushContextQueue");

  // Use a condition variable to wait until the sentinel function executes
  std::mutex mutex;
  std::condition_variable cv;
  bool sentinel_executed = false;

  // Queue a sentinel function that signals completion
  _context_queue->Push([&]() {
    std::lock_guard<std::mutex> lock(mutex);
    sentinel_executed = true;
    cv.notify_one();
  });

  // Wait until the sentinel has been executed by the context thread
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return sentinel_executed; });
}

}  // namespace datadog::impl
