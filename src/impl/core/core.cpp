#include "core/core.hpp"

#include <iostream>
#include <algorithm>
#include <sstream>

#include "platform/clock.hpp"
#include "platform/filesystem.hpp"
#include "platform/http.hpp"
#include "platform/http_writer.hpp"
#include "core/types.hpp"

namespace datadog::impl {

Core::Core(const datadog::CoreConfig& config)
    : _state(CoreState::Uninitialized)
    , _config(config)
    , _context(config)
{
    _features.reserve(16);
}

Core::~Core()
{
}

void Core::SetTrackingConsent(TrackingConsent value)
{
    if (_config.tracking_consent != value)
    {
        // Store the updated consent value, which we use to initialize storage-thread
        // state on core start
        _config.tracking_consent = value;

        // If we're already started, send a message to the storage thread so it can
        // handle the state change
        if (_state == CoreState::Started)
        {
            assert(_storage_queue &&
                "_storage_queue is invalid with CoreState::Started on "
                "SetTrackingConsent"
            );
            _storage_queue->Push(StorageMessage::TrackingConsentChanged(value));
        }
    }
}

void Core::SetService(std::string_view value)
{
    if (_config.service != value)
    {
        // Cache value and update the context; subsequent reports will be generated
        // using the latest context
        _config.service = value;
        _context.SetService(value);
    }
}

void Core::SetEnv(std::string_view value)
{
    if (_config.env != value)
    {
        // Cache value and update the context; subsequent reports will be generated
        // using the latest context
        _config.env = value;
        _context.SetEnv(value);
    }
}

bool Core::Init()
{
    assert(_state == CoreState::Uninitialized && "Core::Init should only be called once");

    // Initialize the filesystem interface, using a temporary directory for testing
    _storage_root = platform::Filesystem::Init("temp-test/storage");
    if (!_storage_root)
    {
        std::cout << "Failed to initialize filesystem\n";
        return false;
    }

    std::vector<std::string> filenames;
    filenames.reserve(64);
    const auto list_files_result = _storage_root->ListFiles(filenames);
    if (list_files_result)
    {
        std::cout << "Got " << filenames.size() << " files from root storage dir.\n" << std::endl;
    }
    else
    {
        const auto err = list_files_result.error();
        std::cout << "Failed to list files in root storage dir: " << static_cast<int>(err) << "\n";
    }

    auto create_subdir_result = _storage_root->PrepareSubdirectory("logs");
    if (create_subdir_result)
    {
        auto subdir = std::move(*create_subdir_result);
        
        auto infile_result = subdir->OpenForRead("foo.dat");
        if (infile_result)
        {
            uint32_t x;
            auto infile = std::move(*infile_result);
            auto read_result = infile->Read(reinterpret_cast<char*>(&x), sizeof(x));
            if (read_result)
            {
                std::cout << "Read int from file: " << x << "\n";
            }
            else
            {
                const auto err = read_result.error();
                std::cout << "Failed to read from open file: " << static_cast<int>(err) << "\n";    
            }
        }
        else
        {
            const auto err = infile_result.error();
            std::cout << "Failed to open file for read: " << static_cast<int>(err) << "\n";
        }

        uint32_t x = 8675309;
        auto outfile_result = subdir->PrepareForWrite("foo.dat");
        if (outfile_result)
        {
            auto outfile = std::move(*outfile_result);
            auto write_result = outfile->Write(reinterpret_cast<const char*>(&x), sizeof(x));
            if (write_result)
            {
                std::cout << "Wrote int to file: " << x << "\n";
            }
            else
            {
                const auto err = write_result.error();
                std::cout << "Failed to write to open file: " << static_cast<int>(err) << "\n";    
            }
        }
        else
        {
            const auto err = outfile_result.error();
            std::cout << "Failed to open file for write: " << static_cast<int>(err) << "\n";
        }
    }
    else
    {
        const auto err = create_subdir_result.error();
        std::cout << "Failed to create subdirectory: " << static_cast<int>(err) << "\n";
    }

    // Initialize the HTTP subsystem
    _http = platform::Http::Init();
    if (!_http)
    {
        std::cout << "Failed to initialize HTTP subsystem\n";
        return false;
    }

    // Create a single HTTP client for testing
    _http_client = _http->CreateClient();
    if (!_http_client)
    {
        std::cout << "Failed to create HTTP client\n";
        return false;
    }

    // Test chunked encoding
    std::stringstream ss;
    ss << "{\"objects\":[{\"value\":0}";
    for (int i = 1; i < 65535; i++)
    {
        ss << "{\"value\":" <<  i << "}";
    }
    ss << "]";
    std::string s = ss.str();

    const platform::HttpResult result = _http_client->Post(
        "http://192.168.0.135:5000/api/v2/something?foo=bar&message=hello%20world",
        "Authorization: Bearer secret\nContent-Type: application/json\n",
        platform::StringWriter{s}
    );

    // Core is initialized; ready to register features and start
    _state = CoreState::Initialized;
    return true;
}

bool Core::RegisterFeature(std::shared_ptr<FeatureBase> impl)
{
    const FeatureId id = impl->GetId();
    const std::string_view name = impl->GetName();

    // Features may only be registered after init but before the core is started
    if (_state != CoreState::Initialized)
    {
        std::cout << "Failed to register feature " << name << " (id " << id << "): core in improper state\n";
        return false;
    }

    // Don't allow a feature to be registered with a duplicate ID (each feature must
    // have a unique ID, and each feature may only be registered once), and don't alllow
    // two features to have the same name, either, as this would cause filesystem
    // contention
    const auto existing = std::find_if(_features.begin(), _features.end(), [&](const Feature& f) {
        return f.id == id || f.name == name;
    });
    if (existing != _features.end())
    {
        std::cout << "Failed to register feature " << name << " (id " << id << "): id or name conflict\n";
        return false;
    }

    // Initialize a subdirectory within our root storage directory that will contain
    // files written on behalf of this feature
    auto feature_subdir = _storage_root->PrepareSubdirectory(name);
    if (!feature_subdir)
    {
        std::cout << "Failed to register feature " << name << " (id " << id << "): feature subdir init failed with error " << static_cast<int>(feature_subdir.error()) << "\n";
        return false;
    }

    // Initialize two subdirectories within that feature directory: one that we'll write
    // to when tracking consent is pending, and another to contain the files that the
    // user has consented to being uploaded: the upload thread will read from the latter
    auto pending_subdir = (*feature_subdir)->PrepareSubdirectory(EventStorage::PENDING_SUBDIRECTORY_NAME);
    if (!pending_subdir)
    {
        std::cout << "Failed to register feature " << name << " (id " << id << "): pending subdir init failed with error " << static_cast<int>(pending_subdir.error()) << "\n";
        return false;
    }
    auto granted_subdir = (*feature_subdir)->PrepareSubdirectory(EventStorage::GRANTED_SUBDIRECTORY_NAME);
    if (!granted_subdir)
    {
        std::cout << "Failed to register feature " << name << " (id " << id << "): granted subdir init failed with error " << static_cast<int>(granted_subdir.error()) << "\n";
        return false;
    }

    // Initialize the EventStorage object that the storage thread will use to persist
    // events to disk as they're generated by this feature implementation
    auto event_storage = std::make_unique<EventStorage>(
        _config.tracking_consent,
        std::make_unique<BatchWriter>(std::move(*pending_subdir)),
        std::make_unique<BatchWriter>(std::move(*granted_subdir))
    );

    _features.emplace_back(
        id,
        name,
        impl,
        std::move(*feature_subdir),
        std::move(event_storage)
    );
    std::cout << "Feature registered: " << name << "(id " << id << ")" << "\n";
    return true;
}

bool Core::Start()
{
    // Start() may only be called after Init(), and while the core is not yet started
    if (_state != CoreState::Initialized)
    {
        std::cout << "Core::Start() called in improper state\n";
        return false;
    }

    // At least one feature must have been registered
    if (_features.empty())
    {
        std::cout << "Core::Start() called with no features registered\n";
        return false;
    }

    // Initialize a thread-safe queue that features can write to whenever they produce
    // events that need to be written to disk
    assert(!_storage_queue && "_storage_queue already exists on Start()");
    _storage_queue = std::make_unique<Queue<StorageMessage>>();

    // Start a thread that will read those events from the queue and write them to
    // persistent storage: the thread accepts non-owning references to the queue and the
    // vector of features, as both are stable for the lifetime of the thread
    assert(!_storage_thread && "_storage_thread already exists on Start()");
    _storage_thread = std::thread(
        StorageThreadMain,
        std::ref(*_storage_queue),
        std::ref(_features)
    );

    std::cout << "Datadog core started.\n";
    std::cout << "- Tracking Consent: " << TrackingConsent_ToString(_config.tracking_consent) << "\n";
    std::cout << "- Site: " << Site_ToString(_config.datadog_site) << "\n";
    std::cout << "- Client Token: " << _config.client_token << "\n";
    std::cout << "- Env: " << _config.env << "\n";
    std::cout << "- Application Version: " << _config.application_version << "\n";
    std::cout << "- Batch Size: " << BatchSize_ToString(_config.batch_size) << "\n";
    std::cout << "- Upload Frequency: " << UploadFrequency_ToString(_config.upload_frequency) << "\n";
    std::cout << "- Batch Processing Level: " << BatchProcessingLevel_ToString(_config.batch_processing_level) << "\n";

    _state = CoreState::Started;

    // Notify each registered feature that the core has started, providing it with a
    // function that it can use to send events to storage
    for (const auto& feature : _features)
    {
        const FeatureId id = feature.id;
        StorageWriter writer = [this, id](Block event, Block event_metadata) -> bool {
            return EnqueueStorageWrite(id, event, event_metadata);
        };
        feature.impl->OnCoreStarted(writer);
    }
    return true;
}

void Core::Shutdown()
{
    // Double-shutdown is fine; just ignore it
    if (_state != CoreState::Started)
    {
        return;
    }

    // Notify each registered feature that the core has stopped
    for (const auto& feature : _features)
    {
        feature.impl->OnCoreStopping();
    }

    // If we were previously started, the storage thread should be running
    assert(_storage_queue && "_storage_queue is invalid on Shutdown");
    assert(_storage_thread && _storage_thread->joinable() &&
        "_storage_thread is non-joinable on Shutdown"
    );

    // Stop all queue processing, then block until the consumer thread drains the queue
    // and exits, at which point it's safe to release the queue
    _storage_queue->Stop();
    _storage_thread->join();
    _storage_thread.reset();
    _storage_queue.reset();

    std::cout << "Datadog core shut down.\n";
    std::cout << "Time at shutdown: " << datadog::platform::Clock::read_utc_nanos() << "\n";

    // Revert to the initialized state; subsequent calls to Start() will restart us
    _state = CoreState::Initialized;
}

bool Core::EnqueueStorageWrite(FeatureId feature_id, Block event, Block event_metadata)
{
    if (_state != CoreState::Started)
    {
        std::cout << "Feature " << feature_id << " attempted to write to storage while core not running\n";
        return false;
    }

    assert(_storage_queue && "_storage_queue is invalid while core is running");
    return _storage_queue->Push(
        StorageMessage::EventGenerated(feature_id, event, event_metadata)
    );
}

}
