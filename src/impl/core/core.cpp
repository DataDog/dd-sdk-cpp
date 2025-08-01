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

void Core::SetService(std::string_view value)
{
    _config.service = value;
    _context.SetService(value);
}

void Core::SetEnv(std::string_view value)
{
    _config.env = value;
    _context.SetEnv(value);
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

    auto create_subdir_result = _storage_root->GetOrCreateChild("logs");
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
        auto outfile_result = subdir->OpenForWrite("foo.dat");
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

bool Core::RegisterFeature(FeatureId id, std::string_view name)
{
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
    auto feature_storage = _storage_root->GetOrCreateChild(name);
    if (!feature_storage)
    {
        std::cout << "Failed to register feature " << name << " (id " << id << "): filesystem init failed with error " << static_cast<int>(feature_storage.error()) << "\n";
        return false;
    }

    _features.emplace_back(id, name, std::move(*feature_storage));
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
    _storage_queue = std::make_unique<Queue<StorageWriteMessage>>();

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
    return true;
}

void Core::Shutdown()
{
    // Double-shutdown is fine; just ignore it
    if (_state != CoreState::Started)
    {
        return;
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

}
