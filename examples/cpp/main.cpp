// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <any>
#include <chrono>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#include "datadog.hpp"

int main()  // NOLINT(bugprone-exception-escape)
{
  // TODO: Compile with -fno-exceptions; clarify exception guarantees; ensure that all
  // exceptions besides std::bad_alloc are practically impossible

  std::cout << "Datadog Native SDK C++ Example\n";

  // Prepare our configuration and create the Datadog SDK Core
  datadog::CoreConfig("fake-client-token", "example-service", "development");
  config.SetApplicationVersion("1.0.0");

  std::string_view client_token = "my-client-token";
  std::string_view service = "my-service";
  std::string_view env = "my-env";

  auto core = datadog::Core::Create(
      datadog::CoreConfig(client_token, service, env)
          .SetApplicationVersion("1.0.0")
          .SetBatchSize(datadog::BatchSize::Small)
          .SetDiagnosticThreshold(datadog::DiagnosticLevel::Debug)
  );
  core->Start();

  std::map<std::string, std::any> extra_attributes = {
      {"foo", 100},
      {"locales", std::vector<std::string>{"en-US", "fr-CA", "fr-FR"}},
      {"nested", std::map<std::string, std::any>{{"x", 42.0f}, {"y", -33.33f}}}
  };

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

  using datadog::Attribute;

  // Start a RUM View
  datadog::Timestamp t;
  uint8_t id[16];

  auto attributes = Attribute::Object(4);
  attributes.SetObjectProperty("mode", Attribute::String("demo"));
  attributes.SetObjectProperty("scaleFactor", Attribute::Double(1.0));
  attributes.SetObjectProperty("refreshId", Attribute::UUID(id));
  attributes.SetObjectProperty("lastRefresh", Attribute::Timestamp(t));

  rum->StartView("main_menu", "Main Menu", attributes);

  // Log messages will now be correlated with our session and view in the RUM UI
  logger->Info("Main menu loaded");

  // Record a RUM Action
  rum->AddAction(datadog::RumActionType::Custom, "Start Menu Navigation");

  // Stop the RUM view
  rum->StopView("main_menu");

  // Stop the core on application shutdown
  std::cout << "Core started successfully. Shutting down...\n";
  core->Stop();

  std::cout << "Example completed successfully\n";

  return 0;
}
