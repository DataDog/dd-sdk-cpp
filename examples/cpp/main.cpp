#include <iostream>

#include <thread>
#include <chrono>

#include "datadog.hpp"

int main()
{
    std::cout << "Datadog Native SDK C++ Example\n";

    datadog::CoreConfig config{
        .tracking_consent = datadog::TrackingConsent::Granted,
        .datadog_site = datadog::Site::us1,
        .client_token = "fake-client-token",
        .service = "example-service",
        .env = "development",
        .application_version = "1.0.0",
        .batch_size = datadog::BatchSize::Medium,
        .upload_frequency = datadog::UploadFrequency::Average,
        .batch_processing_level = datadog::BatchProcessingLevel::Medium
    };

    auto core = datadog::Core::Create(config);
    if (!core)
    {
        std::cout << "Failed to create Datadog core\n";
        return 1;
    }

    auto logging = datadog::Logging::Register(*core);
    if (!logging)
    {
        std::cout << "Failed to register logging\n";
        return 1;
    }

    std::cout << "Starting Datadog core...\n";
    if (!core->Start())
    {
        std::cout << "Failed to start core\n";
        return 1;
    }

    std::cout << "Core started successfully. Shutting down...\n";
    std::this_thread::sleep_for(std::chrono::seconds(60));
    core->Shutdown();

    std::cout << "Example completed successfully\n";

    return 0;
}
