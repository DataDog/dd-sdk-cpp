#include <iostream>

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
    if (!core) {
        std::cout << "Failed to create Datadog core\n";
        return 1;
    }

    auto logging = datadog::Logging::Register(*core);

    std::cout << "Starting Datadog core...\n";
    core->Start();

    std::cout << "Core started successfully. Shutting down...\n";
    core->Shutdown();

    std::cout << "Example completed successfully\n";

    return 0;
}
