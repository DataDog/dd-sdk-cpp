// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <chrono>
#include <iostream>
#include <thread>

#include "datadog.hpp"

int main()  // NOLINT(bugprone-exception-escape)
{
  // TODO: Compile with -fno-exceptions; clarify exception guarantees; ensure that all
  // exceptions besides std::bad_alloc are practically impossible

  std::cout << "Datadog Native SDK C++ Example\n";

  // Prepare our configuration and create the Datadog SDK Core
  datadog::CoreConfig config{
      datadog::TrackingConsent::Granted,
      datadog::Site::us1,
      "fake-client-token",
      "example-service",
      "development",
      "1.0.0",
      datadog::BatchSize::Medium,
      datadog::UploadFrequency::Average,
      datadog::BatchProcessingLevel::Medium,
      1,
      ""
  };

  auto core = datadog::Core::Create(config);
  if (!core) {
    std::cout << "Failed to create Datadog core\n";
    return 1;
  }

  // Register the logging feature
  auto logging = datadog::Logging::Register(core);
  if (!logging) {
    std::cout << "Failed to register logging\n";
    return 1;
  }

  // Create a logger (this can be done before or after Core start)
  auto logger = logging->CreateLogger();
  if (!logger) {
    std::cout << "Failed to create logger\n";
    return 1;
  }

  // Start the core to begin processing events
  std::cout << "Starting Datadog core...\n";
  if (!core->Start()) {
    std::cout << "Failed to start core\n";
    return 1;
  }

  // Use our logger to send a message
  logger->Info("Hello world!");

  // Stop the core on application shutdown
  std::cout << "Core started successfully. Shutting down...\n";
  core->Stop();

  std::cout << "Example completed successfully\n";

  return 0;
}
