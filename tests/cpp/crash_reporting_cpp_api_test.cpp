// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "datadog/core.hpp"
#include "datadog/crash_reporting.hpp"

using namespace datadog;

// Registering Crash Reporting with a working core would install a process-global crash
// handler in the test process, so crash reporting behavior is covered by the
// integration tests instead. These tests cover only calls without a valid core.

TEST_CASE("CrashReporting::Register null safety", "[unit][crash_reporting][cpp-api]") {
  SECTION("M return a no-op interface W the core is invalid") {
    // Given both a Core interface that has no valid implementation, and a null pointer
    CoreConfig invalid_config("", "", "");
    invalid_config.SetDiagnosticHandler(nullptr);
    std::shared_ptr<Core> noop_core =
        Core::Create(invalid_config, TrackingConsent::Granted);
    std::shared_ptr<Core> null_core;
    std::vector<std::shared_ptr<Core>> cores = {noop_core, null_core};

    // And a config with a custom handler path
    CrashReportingConfig config;
    config.SetHandlerExePath("crashpad_handler");

    for (const std::shared_ptr<Core>& core : cores) {
      // When we register the Crash Reporting feature on it
      auto crash_reporting = CrashReporting::Register(core, config);

      // Then we get a valid object, whose destruction is harmless
      REQUIRE(crash_reporting != nullptr);
    }
  }
}
