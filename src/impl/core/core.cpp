#include "core/core.hpp"

#include "platform/clock.hpp"
#include "platform/http.hpp"
#include "core/types.hpp"

#include <iostream>
#include <algorithm>
#include <sstream>

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
    // Initialize the HTTP subsystem
    _http = platform::Http::Init();
    if (!_http)
    {
        return false;
    }

    // Create a single HTTP client for testing
    _http_client = _http->CreateClient();
    if (!_http_client)
    {
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
    size_t offset = 0;
    platform::HttpBodyWriter writer = [&](char* buffer, size_t num_bytes) -> size_t
    {
        printf("Offset is %zu\n", offset);

        size_t num_bytes_to_copy = s.length() - offset;
        if (num_bytes_to_copy == 0)
        {
            printf("Returning 0\n");
            return 0;
        }

        if (num_bytes_to_copy > num_bytes)
        {
            num_bytes_to_copy = num_bytes;
        }

        memcpy(buffer, s.data() + offset, num_bytes_to_copy);
        offset += num_bytes_to_copy;
        printf("Wrote %zu bytes; offset now %zu; returning %zu\n", num_bytes_to_copy, offset, num_bytes_to_copy);
        return num_bytes_to_copy;
    };


    const platform::HttpResult result = _http_client->Post(
        "http://192.168.0.135:5000/api/v2/something?foo=bar&message=hello%20world",
        "Authorization: Bearer secret\nContent-Type: application/json\n",
        writer
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
