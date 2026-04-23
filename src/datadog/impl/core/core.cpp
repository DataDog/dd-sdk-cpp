// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/core.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "datadog/impl/core/context_thread.hpp"
#include "datadog/impl/core/messaging_thread.hpp"
#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/core/platform/http.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/storage_thread.hpp"
#include "datadog/impl/core/types.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/version.hpp"

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

  // Create an instance of IFilesystem, which wraps platform-specific APIs for examining
  // and modifying directories and files, performing file I/O, and managing advisory
  // locks on files
  std::unique_ptr<IFilesystem> fs = CreateFilesystem();
  if (!fs) {
    return nonstd::make_unexpected(
        ErrorMessage("filesystem interface could not be initialized")
    );
  }

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
      std::move(clock), std::move(fs), std::move(http), std::move(system_info)
  );
}

Core::Core(const CoreConfig& config, CoreSubsystems&& subsystems)
    : _config(config),
      _immutable_context(
          config,
          subsystems.system_info->GetOsInfo(),
          subsystems.system_info->GetDeviceInfo(),
          subsystems.http->GetName(),
          subsystems.http->GetVersion()
      ),
      _diagnostic_logger(config.diagnostic_handler, config.diagnostic_threshold),
      _context_provider(
          std::make_unique<CoreContextProvider>(CoreContext(_immutable_context))
      ),
      _subsystems(std::move(subsystems)) {
  DATADOG_ASSERT(_subsystems.fs, "Core created with no filesystem interface");
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

  // Required subsystems are injected on construction; they should all be present
  DATADOG_ASSERT(_subsystems.system_info != nullptr, "Core has no ISystemInfo on Init");
  DATADOG_ASSERT(_subsystems.http != nullptr, "Core has no IHttpSubsystem on Init");
  DATADOG_ASSERT(_subsystems.fs != nullptr, "Core has no IFilesystem on Init");

  // Get the PID value for the process in which we're running
  const int64_t pid = _subsystems.system_info->GetPid();
  if (pid <= 0) {
    // In practice, getpid() and GetCurrentProcessId() are guaranteed to return a
    // positive nonzero value
    _diagnostic_logger.Error(
        "Core initialization failed: got invalid PID", {{"pid", pid}}
    );
    return false;
  }

  // Create a single HTTP client
  _http_client = _subsystems.http->CreateClient();
  if (!_http_client) {
    _diagnostic_logger.Error(
        "Core initialization failed: could not create HTTP client"
    );
    return false;
  }

  // Initialize a storage directory for this SDK instance, beneath the configured
  // application storage directory
  // TODO(RUM-15284): SdkStorage::Initialize migrates old processes' event data into our
  // new storage directory- this may scale with the amount of leftover data, and could
  // potentially be offloaded to the storage thread to avoid blocking SDK init.
  _storage.emplace(*_subsystems.fs, _diagnostic_logger, pid);
  if (!_storage->Initialize(_config.event_storage_location, "main")) {
    _diagnostic_logger.Error(
        "Core initialization failed: could not initialize SDK storage"
    );
    return false;
  }

  // Core is initialized; ready to register features and start
  _state = CoreState::Initialized;
  _diagnostic_logger.Debug("Core initialization complete");
  return true;
}

std::unique_ptr<ArtifactStorage> Core::InitializeArtifactStorage(
    std::string_view directory_name
) {
  if (!_storage) {
    // This method is only used by the API implementation (it's not exposed publicly),
    // and we only support initializing artifact storage during feature registration
    DATADOG_ASSERT(
        false, "InitializeArtifactStorage called when no SdkStorage is present"
    );
    return nullptr;
  }
  return _storage->InitializeArtifactStorage(directory_name);
}

bool Core::RegisterFeature(const std::shared_ptr<Feature>& impl) {
  // Interrogate the feature to get its identifying details: FourCC ID for quick
  // comparison internally; short, lowercase feature name for logs, event storage
  // directory, etc.
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

  // Successful SdkStorage initialization is a precondition of successful Core::Init():
  // since State == Initialized, SdkStorage must be valid
  DATADOG_ASSERT(_subsystems.fs, "IFilesystem uninitialized on feature registration");
  DATADOG_ASSERT(
      _storage.has_value(), "SdkStorage uninitialized on feature registration"
  );

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

  // Prepare a FeatureEventStorage interface, creating the requisite storage directories
  // on disk for this feature's events
  auto events = _storage->InitializeFeatureEventStorage(name);
  if (!events) {
    // FeatureEventStorage will log more descriptive diagnostic messages in the event of
    // failure; just log the practical result for us (can't register feature) and abort
    _diagnostic_logger.Error(
        "Failed to register feature: could not initialize event storage",
        {{"feature", name}, {"feature_id", static_cast<int64_t>(id)}}
    );
    return false;
  }

  // Initialize the BatchWriter object that the storage thread will use to persist
  // events to disk as they're generated by this feature implementation
  auto writer_config = BatchWriterConfig::FromBatchSize(_config.batch_size);
  auto batch_writer = std::make_unique<BatchWriter>(
      _diagnostic_logger,
      _config.tracking_consent,
      *_subsystems.fs,
      *events,
      *_subsystems.clock,
      writer_config
  );

  // Initialize the feature-specific state used by the upload thread
  DATADOG_ASSERT(_context_provider, "_context_provider is null on RegisterFeature()");
  auto upload_state = std::make_unique<UploadThreadState>(
      _context_provider->Get(), _config.upload_frequency
  );

  _features.emplace_back(
      id,
      name,
      std::move(events),
      impl,
      std::move(batch_writer),
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

  // Reset any feature-specific context left over from a previous run, so that features
  // start clean rather than inheriting stale state from the prior session
  DATADOG_ASSERT(_context_provider, "_context_provider is null on Start()");
  _context_provider->Update([](CoreContext& ctx) { ctx.Reset(); });

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

  // Iterate through all registered features and call MakeMessageHandler(), allowing
  // features to register their intent to be notified when messages are sent on the
  // message bus
  std::vector<std::function<void(const FeatureMessage&)>> handlers;
  handlers.reserve(_features.size());
  for (const auto& f : _features) {
    if (auto h = f.impl->MakeMessageHandler()) {
      handlers.push_back(std::move(*h));
    }
  }

  // Create the message bus, which will queue all core-to-feature and feature-to-feature
  // messages that need to be handled by interested features
  DATADOG_ASSERT(!_message_bus, "_message_bus already exists on Start()");
  _message_bus = std::make_unique<MessageBus>(std::move(handlers));

  // Install the MessageBus into the CoreContextProvider so that it can send
  // ContextChangedMessage in response to updates: context thread doesn't exist yet, so
  // no synchronization is required here
  _context_provider->SetMessageBus(_message_bus.get());

  // Initialize a thread-safe queue for feature-submitted functions that will execute on
  // the context thread
  DATADOG_ASSERT(!_context_queue, "_context_queue already exists on Start()");
  _context_queue = std::make_unique<Queue<std::function<void()>>>();

  // Start the context thread that will execute functions submitted by features
  DATADOG_ASSERT(!_context_thread, "_context_thread already exists on Start()");
  _context_thread = std::thread(
      ContextThreadMain, std::ref(_diagnostic_logger), std::ref(*_context_queue)
  );

  // Initialize an upload scheduler to manage the timing of upload cycles for each
  // feature
  DATADOG_ASSERT(!_upload_scheduler, "_upload_scheduler already exists on Start()");
  _upload_scheduler = std::make_unique<UploadScheduler>(*_subsystems.clock);

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
      std::ref(*_subsystems.clock),
      std::ref(*_upload_scheduler),
      std::ref(_features),
      std::ref(*_subsystems.fs),
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

  // Initialize a callback that will allow each feature to produce FeatureMessage values
  // to the MessageBus in order to notify other features of relevant state changes
  MessagePublisher message_publisher = [this](FeatureMessage message) -> bool {
    if (_message_bus) {
      return _message_bus->Send(std::move(message));
    }
    return false;
  };

  // Notify each registered feature that the core has started, providing it with a
  // FeatureScope interface that it can use to interoperate with the core
  for (const auto& feature : _features) {
    const FeatureId id = feature.id;
    EventWriter event_writer = [this, id](Block event, Block event_metadata) -> bool {
      return EnqueueStorageWrite(id, event, event_metadata);
    };
    feature.impl->OnCoreStarted(
        FeatureScope::Create(
            *_context_provider,
            event_writer,
            message_publisher,
            _diagnostic_logger,
            *_context_queue
        )
    );
  }

  // Start the messaging thread only after all features have completed OnCoreStarted().
  // This ensures that any ContextChangedMessages dispatched during startup (e.g. from
  // a feature calling UpdateContext in its Start()) are not delivered to a feature's
  // handler before that feature's own OnCoreStarted() has run. Messages enqueued
  // during the loop above are buffered in the queue and will be drained once this
  // thread starts.
  DATADOG_ASSERT(!_message_bus_thread, "_message_bus_thread already exists on Start()");
  _message_bus_thread = std::thread(
      MessagingThreadMain, std::ref(_diagnostic_logger), std::ref(*_message_bus)
  );

  return true;
}

void Core::Stop() {
  // Double-shutdown is fine; just ignore it
  if (_state != CoreState::Started) {
    return;
  }
  _diagnostic_logger.Debug("Beginning Core shutdown");

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
  // thread or tear down feature state.
  // TODO(RUM-15042): Other SDKs abandon the context thread and shut down in a
  // non-blocking fashion, without draining the context queue
  _context_queue->Stop();
  if (_context_thread) {
    _diagnostic_logger.Debug("Joining on context thread");
    _context_thread->join();
  }
  _context_thread.reset();

  // Stop the messaging thread after the context thread exits: the context thread is
  // the primary producer of messages (via Update()), so draining it first prevents new
  // messages from arriving after we signal the messaging thread to stop. After joining,
  // detach the bus from the context provider so any stray Update() calls (which should
  // not happen at this point) do not reference the destroyed bus.
  DATADOG_ASSERT(_message_bus, "_message_bus is invalid on Stop");
  DATADOG_ASSERT(
      _message_bus_thread && _message_bus_thread->joinable(),
      "_message_bus_thread is non-joinable on Stop"
  );
  _message_bus->Stop();
  _diagnostic_logger.Debug("Joining on messaging thread");
  _message_bus_thread->join();
  _message_bus_thread.reset();
  _context_provider->SetMessageBus(nullptr);

  // Notify each registered feature that the core has stopped, now that no more
  // context-thread functions enqueued by those features may be running
  for (const auto& feature : _features) {
    feature.impl->OnCoreStopping();
  }

  // Destroy the context queue and message bus, now that no more features exist
  _context_queue.reset();
  _message_bus.reset();

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

    // Run our upload cycle procedure on the main thread, synchronously: now that
    // we've joined on both threads, all state is synchronized
    for (const auto& feature : _features) {
      Internal_HandleUploadProc(
          _diagnostic_logger,
          flush_config,
          *_subsystems.clock,
          feature.id,
          _features,
          *_subsystems.fs,
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

IFilesystem& Core::GetFilesystem() const {
  DATADOG_ASSERT(
      _state >= CoreState::Initialized, "GetFilesystem called before Core init"
  );
  DATADOG_ASSERT(_subsystems.fs, "IFilesystem not present after Core init");
  return *_subsystems.fs;
}

std::string_view Core::GetServiceName() const {
  DATADOG_ASSERT(
      _state >= CoreState::Initialized, "GetServiceName called before Core init"
  );
  return _config.service;
}

std::string_view Core::GetApplicationVersion() const {
  DATADOG_ASSERT(
      _state >= CoreState::Initialized, "GetApplicationVersion called before Core init"
  );
  return _config.application_version;
}

}  // namespace datadog::impl
