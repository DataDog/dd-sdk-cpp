#include <iostream>

#include <chrono>
#include <thread>

#include "datadog.hpp"

int main() // NOLINT(bugprone-exception-escape)
{
    // TODO: Compile with -fno-exceptions; clarify exception guarantees; ensure that all
    // exceptions besides std::bad_alloc are practically impossible

    std::cout << "Datadog Native SDK C++ Example\n";

    datadog::CoreConfig config{ datadog::TrackingConsent::Granted,
                                datadog::Site::us1,
                                "fake-client-token",
                                "example-service",
                                "development",
                                "1.0.0",
                                datadog::BatchSize::Medium,
                                datadog::UploadFrequency::Average,
                                datadog::BatchProcessingLevel::Medium };

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
    core->Stop();

    std::cout << "Example completed successfully\n";

    return 0;
}
