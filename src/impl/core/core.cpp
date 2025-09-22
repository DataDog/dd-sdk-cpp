// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/core.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "assert.hpp"
#include "core/storage_thread.hpp"
#include "core/types.hpp"
#include "platform/clock.hpp"
#include "platform/filesystem.hpp"
#include "platform/http.hpp"

namespace datadog::impl {

std::optional<CoreSubsystems> CoreSubsystems::Init(const CoreConfig& config) {
  // TODO: Allow configuration of storage path via core; use sensible default
  // per-platform
  (void)config;

  // Prepare a wrapper object that can read from the system clock
  auto clock = platform::Clock::Init();
  if (!clock) {
    // TODO: Proper configurable logging via telemetry logger, user-provided log
    // callbacks, etc
    std::cout << "Failed to initialize clock\n";
    return std::nullopt;
  }

  // Store event data in "$(pwd)/.datadog/<feature>" by default
  const std::string_view DEFAULT_STORAGE_DIR = ".datadog";
  auto storage_root = platform::Filesystem::Init(DEFAULT_STORAGE_DIR);
  if (!storage_root) {
    std::cout << "Failed to initialize event storage subsystem\n";
    return std::nullopt;
  }

  // Prepare whatever HTTP client library we'll use to create HTTP clients
  auto http = platform::Http::Init();
  if (!http) {
    std::cout << "Failed to initialize HTTP subsystem\n";
    return std::nullopt;
  }

  // Return our newly-created subsystems, to be transferred into the Core
  return CoreSubsystems(std::move(clock), std::move(storage_root), std::move(http));
}

Core::Core(const CoreConfig& config, CoreSubsystems&& subsystems)
    : _config(config), _context(config), _subsystems(std::move(subsystems)) {
  DATADOG_ASSERT(
      _subsystems.storage_root, "Core created with no root storage directory"
  );
  DATADOG_ASSERT(_subsystems.http, "Core created with no HTTP subsystem");

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
      // Otherwise, notify each feature directly, since EventStorage is initialized from
      // config on RegisterFeature(), not on Start()
      for (auto& feature : _features) {
        if (!feature.event_storage->SetTrackingConsent(value)) {
          std::cout << "Failed to set tracking consent synchronously\n";
        }
      }
    }
  }
}

void Core::SetService(std::string_view value) {
  // TODO: Push context changes to upload thread; requires synchronization
  if (_config.service != value) {
    // Cache value and update the context; subsequent reports will be generated using
    // the latest context
    _config.service = value;
    _context.SetService(value);
  }
}

void Core::SetEnv(std::string_view value) {
  // TODO: Push context changes to upload thread; requires synchronization
  if (_config.env != value) {
    // Cache value and update the context; subsequent reports will be generated using
    // the latest context
    _config.env = value;
    _context.SetEnv(value);
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
    std::cout << "Failed to create HTTP client\n";
    return false;
  }

  // Core is initialized; ready to register features and start
  _state = CoreState::Initialized;
  return true;
}

bool Core::RegisterFeature(const std::shared_ptr<Feature>& impl) {
  const FeatureId id = impl->GetId();
  const std::string_view name = impl->GetName();

  // Features may only be registered after init but before the core is started
  if (_state != CoreState::Initialized) {
    std::cout << "Failed to register feature " << name << " (id " << id
              << "): core in improper state\n";
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
    std::cout << "Failed to register feature " << name << " (id " << id
              << "): id or name conflict\n";
    return false;
  }

  // Initialize a subdirectory within our root storage directory that will contain files
  // written on behalf of this feature
  auto feature_subdir = _subsystems.storage_root->PrepareSubdirectory(name);
  if (!feature_subdir) {
    std::cout << "Failed to register feature " << name << " (id " << id
              << "): feature subdir init failed with error "
              << static_cast<int>(feature_subdir.error()) << "\n";
    return false;
  }

  // Initialize two subdirectories within that feature directory: one that we'll write
  // to when tracking consent is pending, and another to contain the files that the user
  // has consented to being uploaded: the upload thread will read from the latter
  auto pending_subdir =
      (*feature_subdir)->PrepareSubdirectory(EventStorage::PENDING_SUBDIRECTORY_NAME);
  if (!pending_subdir) {
    std::cout << "Failed to register feature " << name << " (id " << id
              << "): pending subdir init failed with error "
              << static_cast<int>(pending_subdir.error()) << "\n";
    return false;
  }
  auto granted_subdir =
      (*feature_subdir)->PrepareSubdirectory(EventStorage::GRANTED_SUBDIRECTORY_NAME);
  if (!granted_subdir) {
    std::cout << "Failed to register feature " << name << " (id " << id
              << "): granted subdir init failed with error "
              << static_cast<int>(granted_subdir.error()) << "\n";
    return false;
  }

  // Initialize the EventStorage object that the storage thread will use to persist
  // events to disk as they're generated by this feature implementation
  auto writer_config = BatchWriterConfig::FromBatchSize(_config.batch_size);
  auto event_storage = std::make_unique<EventStorage>(
      _config.tracking_consent,
      std::make_unique<BatchWriter>(
          std::move(*pending_subdir), *_subsystems.clock, writer_config
      ),
      std::make_unique<BatchWriter>(
          std::move(*granted_subdir), *_subsystems.clock, writer_config
      )
  );

  // Prepare a separate interface to the directory that the upload thread should read
  // from: this is the same location on disk that the storage thread may write to (i.e.
  // granted_subdir), but we want each thread to have its own handle
  auto event_read_directory =
      (*feature_subdir)->PrepareSubdirectory(EventStorage::GRANTED_SUBDIRECTORY_NAME);
  if (!event_read_directory) {
    std::cout << "Failed to register feature " << name << " (id " << id
              << "): event read subdir init failed with error "
              << static_cast<int>(event_read_directory.error()) << "\n";
    return false;
  }

  // Initialize the feature-specific state used by the upload thread
  auto upload_state = std::make_unique<UploadThreadState>(_config.upload_frequency);

  _features.emplace_back(
      id, name, impl, std::move(*feature_subdir), std::move(event_storage),
      std::move(*event_read_directory), std::move(upload_state)
  );
  std::cout << "Feature registered: " << name << "(id " << id << ")" << "\n";
  return true;
}

bool Core::Start() {
  // Start() may only be called after Init(), and while the core is not yet started
  if (_state != CoreState::Initialized) {
    std::cout << "Core::Start() called in improper state\n";
    return false;
  }

  // At least one feature must have been registered
  if (_features.empty()) {
    std::cout << "Core::Start() called with no features registered\n";
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
  _storage_thread =
      std::thread(StorageThreadMain, std::ref(*_storage_queue), std::ref(_features));

  DATADOG_ASSERT(!_upload_scheduler, "_upload_scheduler already exists on Start()");
  _upload_scheduler = std::make_unique<UploadScheduler>(*_subsystems.clock);

  // Start another thread that will schedule periodic upload cycles on a per-feature
  // basis: each time an upload cycle runs, the thread will check the relevant storage
  // directory for batches of events that are ready for read, processing them via the
  // feature implementation and sending them to intake over HTTP
  DATADOG_ASSERT(!_upload_thread, "_upload_thread already exists on Start()");
  _upload_thread = std::thread(
      UploadThreadMain,
      UploadThreadConfig::FromCoreConfig(
          _config.batch_size, _config.batch_processing_level
      ),
      std::ref(_context), std::ref(*_subsystems.clock), std::ref(*_upload_scheduler),
      std::ref(_features), std::ref(*_http_client)
  );

  std::cout << "Datadog core started.\n";
  std::cout << "- Tracking Consent: "
            << TrackingConsent_ToString(_config.tracking_consent) << "\n";
  std::cout << "- Site: " << Site_ToString(_config.site) << "\n";
  std::cout << "- Client Token: " << _config.client_token << "\n";
  std::cout << "- Env: " << _config.env << "\n";
  std::cout << "- Application Version: " << _config.application_version << "\n";
  std::cout << "- Batch Size: " << BatchSize_ToString(_config.batch_size) << "\n";
  std::cout << "- Upload Frequency: "
            << UploadFrequency_ToString(_config.upload_frequency) << "\n";
  std::cout << "- Batch Processing Level: "
            << BatchProcessingLevel_ToString(_config.batch_processing_level) << "\n";

  _state = CoreState::Started;

  // Notify each registered feature that the core has started, providing it with a
  // function that it can use to send events to storage
  for (const auto& feature : _features) {
    const FeatureId id = feature.id;
    EventGeneratedFunc event_callback =
        [this, id](Block event, Block event_metadata) -> bool {
      return EnqueueStorageWrite(id, event, event_metadata);
    };
    feature.impl->OnCoreStarted(event_callback);
  }
  return true;
}

void Core::Stop() {
  // Double-shutdown is fine; just ignore it
  if (_state != CoreState::Started) {
    return;
  }

  // Notify each registered feature that the core has stopped
  for (const auto& feature : _features) {
    feature.impl->OnCoreStopping();
  }

  // If we were previously started, the storage and upload threads should be running
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

  // Stop all queue processing, then block until the consumer thread drains the queue
  // and exits, at which point it's safe to release the queue
  _storage_queue->Stop();
  if (_storage_thread) {
    _storage_thread->join();
  }
  _storage_thread.reset();
  _storage_queue.reset();

  // Once the storage thread is fully shut down, signal to the upload thread (with
  // synchronization) that it should exit, then wait for it to do so
  _upload_scheduler->Stop();
  if (_upload_thread) {
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
    platform::Duration min_file_age_for_read{0};
    UploadThreadConfig flush_config(
        min_file_age_for_read,
        _config.internal_options.num_http_requests_per_feature_to_flush_on_stop
    );

    // We can't reuse buffers from the upload thread (unless we factor them out and make
    // them owned by the core)
    std::vector<std::string> mut_filenames;
    std::vector<char> mut_read_buffer;

    // Run our upload cycle procedure on the main thread, synchronously: now that we've
    // joined on both threads, all state is synchronized
    for (const auto& feature : _features) {
      Internal_HandleUploadProc(
          flush_config, _context, *_subsystems.clock, feature.id, _features,
          *_http_client, mut_filenames, mut_read_buffer
      );
    }
  }

  std::cout << "Datadog core stopped.\n";
  std::cout << "Time at shutdown: "
            << _subsystems.clock->Now().time_since_epoch().count() << "\n";

  // Revert to the initialized state; subsequent calls to Start() will restart us
  _state = CoreState::Initialized;
}

bool Core::EnqueueStorageWrite(
    FeatureId feature_id, Block event, Block event_metadata
) {
  if (_state != CoreState::Started) {
    std::cout << "Feature " << feature_id
              << " attempted to write to storage while core not running\n";
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

  return _context.service;
}

std::string_view Core::GetApplicationVersion() const {
  DATADOG_ASSERT(
      _state >= CoreState::Initialized, "GetApplicationVersion called before Core init"
  );

  return _context.application_version;
}

}  // namespace datadog::impl
