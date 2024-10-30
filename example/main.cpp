// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include <datadog/core.h>
#include <datadog/logging.h>

// NOLINTBEGIN(google-build-using-namespace)
using namespace datadog::core;
using namespace datadog::logging;
using namespace std::string_view_literals;
// NOLINTEND(google-build-using-namespace)

int main() {
  DatadogConfiguration config{TrackingConsent::Granted,
                              /* client token */ std::string(""),
                              std::string("com.datadoghq.example.cpp"),
                              std::string("prod"), std::string("1.0.0")};
  auto datadog_core = DatadogCore::Create(config);

  if (auto logging = datadog_core->RegisterFeature<DatadogLogging>()) {
    DatadogLogConfiguration logger_config{};

    if (auto logger = logging->CreateLogger(logger_config)) {
      logger->Debug("Info log");
      logger->Info("Info log");
      logger->Info("Warn log");
      logger->Error("Error log");
    }

    // TODO(jeff.ward): Create a flush function
  }

  return 0;
}
