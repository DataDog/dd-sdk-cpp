#include "core/core.hpp"

#include <iostream>
#include <algorithm>
#include <sstream>

#include "platform/clock.hpp"
#include "platform/filesystem.hpp"
#include "platform/http.hpp"
#include "platform/http_writer.hpp"
#include "core/types.hpp"

namespace datadog {

impl::Core::Core(const impl::CoreConfig& config)
    : _config(config)
{
    _features.reserve(16);
}

impl::Core::~Core()
{
}

bool impl::Core::Start()
{
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

    std::cout << "Datadog core started.\n";
    std::cout << "- Tracking Consent: " << TrackingConsent_ToString(_config.tracking_consent) << "\n";
    std::cout << "- Site: " << Site_ToString(_config.datadog_site) << "\n";
    std::cout << "- Client Token: " << _config.client_token << "\n";
    std::cout << "- Env: " << _config.env << "\n";
    std::cout << "- Application Version: " << _config.application_version << "\n";
    std::cout << "- Batch Size: " << BatchSize_ToString(_config.batch_size) << "\n";
    std::cout << "- Upload Frequency: " << UploadFrequency_ToString(_config.upload_frequency) << "\n";
    std::cout << "- Batch Processing Level: " << BatchProcessingLevel_ToString(_config.batch_processing_level) << "\n";



    return true;
}

void impl::Core::Shutdown()
{
    std::cout << "Datadog core shut down.\n";
    std::cout << "Time at shutdown: " << datadog::platform::Clock::read_utc_nanos() << "\n";
}

void impl::Core::RegisterFeature(impl::Feature&& feature)
{
    const auto existing = std::find_if(_features.begin(), _features.end(), [&](const Feature& f) {
        return f.id == feature.id;
    });
    if (existing != _features.cend())
    {
        return;
    }

    std::cout << "Feature registered: " << feature.name << "\n";

    _features.emplace_back(std::move(feature));
}

}
