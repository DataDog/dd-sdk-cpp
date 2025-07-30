#include "core/core.hpp"

#include "platform/clock.hpp"
#include "core/types.hpp"

#include <iostream>
#include <algorithm>

namespace datadog {

impl::Core::Core(const impl::CoreConfig& config)
    : _config(config)
{
    _features.reserve(16);
}

void impl::Core::Start()
{
    std::cout << "Datadog core started.\n";
    std::cout << "- Tracking Consent: " << TrackingConsent_ToString(_config.tracking_consent) << "\n";
    std::cout << "- Site: " << Site_ToString(_config.datadog_site) << "\n";
    std::cout << "- Client Token: " << _config.client_token << "\n";
    std::cout << "- Env: " << _config.env << "\n";
    std::cout << "- Application Version: " << _config.application_version << "\n";
    std::cout << "- Batch Size: " << BatchSize_ToString(_config.batch_size) << "\n";
    std::cout << "- Upload Frequency: " << UploadFrequency_ToString(_config.upload_frequency) << "\n";
    std::cout << "- Batch Processing Level: " << BatchProcessingLevel_ToString(_config.batch_processing_level) << "\n";
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
