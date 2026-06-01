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
  datadog::CoreConfig config("fake-client-token", "example-service", "development");
  config.SetApplicationVersion("1.0.0");

  auto core = datadog::Core::Create(config, datadog::TrackingConsent::Pending);
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

  // Register the RUM feature
  auto rum = datadog::Rum::Register(
      core, datadog::RumConfig("a991ca10-4004-4004-4004-beefbeefbeef")
  );
  if (!rum) {
    // TODO: null checks are unnecessary
    std::cout << "Failed to register RUM\n";
    return 1;
  }

  // Start the core to begin processing events
  std::cout << "Starting Datadog core...\n";
  if (!core->Start()) {
    std::cout << "Failed to start core\n";
    return 1;
  }

  // Whenever the user's tracking consent changes, convey it to the SDK
  core->SetTrackingConsent(datadog::TrackingConsent::Granted);

  // Use our logger to send a message
  logger->Info("Hello world!");

  // Start a RUM View
  rum->StartView("main_menu");

  // Log messages will now be correlated with our session and view in the RUM UI
  logger->Info("Main menu loaded");

  // Record a RUM Action
  rum->AddAction(datadog::RumActionType::Custom, "Start Menu Navigation");

  // Track a operation that succeeds
  rum->StartOperation("Checkout");
  rum->SucceedOperation("Checkout");

  // Track a operation that fails
  rum->StartOperation("Upload", "profile-photo");
  rum->FailOperation(
      "Upload", datadog::RumOperationFailureReason::Error, "profile-photo"
  );

  // Stop the RUM view
  rum->StopView("main_menu");

  // Stop the core on application shutdown
  std::cout << "Core started successfully. Shutting down...\n";
  core->Stop();

  std::cout << "Example completed successfully\n";

  return 0;
}
