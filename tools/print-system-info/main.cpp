// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <iostream>

#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

/**
 * This program is a simple utility that prints the values resolved by the SystemInfo
 * interface for the current runtime environment.
 *
 * Example:
 *
 * > ./dd_system_info
 * os.name: macOS
 * os.version: 15.7
 * os.build: 24G222
 * os.version_major: 15
 */
int main() {
  // Log all diagnostic output encountered during SystemInfo::Init to stderr
  datadog::impl::DiagnosticLogger logger{
      [](const datadog::DiagnosticMessage& message) {
        std::cerr << message.text << "\n";
      },
      datadog::DiagnosticLevel::Debug
  };

  // Initialize the SystemInfo implementation (whichever is compiled into this build)
  // and retrieve OS and device info
  auto system_info = datadog::platform::SystemInfo::Init(logger);
  const auto& os = system_info->GetOsInfo();
  const auto& device = system_info->GetDeviceInfo();

  std::cout << "os.name: " << os.name << "\n"
            << "os.version: " << os.version << "\n"
            << "os.build: " << os.build << "\n"
            << "os.version_major: " << os.version_major << "\n"
            << "device.type: " << device.type << "\n"
            << "device.name: " << device.name << "\n"
            << "device.model: " << device.model << "\n"
            << "device.brand: " << device.brand << "\n"
            << "device.architecture: " << device.architecture << "\n"
            << "device.locale: " << device.locale << "\n"
            << "device.time_zone: " << device.time_zone << "\n";

  return 0;
}
