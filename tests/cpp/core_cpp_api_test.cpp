// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "datadog/core.hpp"

using namespace datadog;

TEST_CASE("Core null safety", "[unit][core][cpp-api]") {
  SECTION("M safely do nothing W this wraps nullptr") {
    // Given a CoreConfig that lacks required parameters
    CoreConfig config("", "", "");

    // When we create a Core from that config
    auto core = Core::Create(config);

    // Then we get a valid object that handles all member functions calls as a no-op
    REQUIRE(core != nullptr);
    REQUIRE(core->Start() == false);
    core->SetTrackingConsent(datadog::TrackingConsent::Granted);
    core->Stop();
  }
}

TEST_CASE("Core validation", "[unit][core][cpp-api]") {
  // Capture diagnostic messages that would be printed to stderr by default
  std::vector<DiagnosticMessage> diagnostics;
  auto diagnostic_handler = [&](const DiagnosticMessage& message) {
    diagnostics.push_back(message);
  };

  SECTION("M accept config W initialized with required values") {
    // Given a config struct that's been initialized with the bare-minimum set of values
    CoreConfig config("my-client-token", "my-service", "my-env");
    config.SetDiagnosticHandler(diagnostic_handler);

    // When we attempt to create a core from that config
    auto core = Core::Create(config);

    // Then no diagnostic warnings/errors are emitted
    REQUIRE(diagnostics.empty());
  }

  SECTION("M reject config W client_token is missing") {
    // Given a config struct that's missing a client_token value
    CoreConfig config("", "my-service", "my-env");
    config.SetDiagnosticHandler(diagnostic_handler);

    // When we attempt to create a core from that config
    auto core = Core::Create(config);

    // Then we receive a diagnostic error
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics[0].level == DiagnosticLevel::Error);
    REQUIRE(
        std::string_view{diagnostics[0].text} ==
        "SDK initialization failed: application must supply a non-empty 'client_token' "
        "value in CoreConfig"
    );
  }

  SECTION("M reject config W service is missing") {
    // Given a config struct that's missing a service value
    CoreConfig config("my-client-token", "", "my-env");
    config.SetDiagnosticHandler(diagnostic_handler);

    // When we attempt to create a core from that config
    auto core = Core::Create(config);

    // Then we receive a diagnostic error
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics[0].level == DiagnosticLevel::Error);
    REQUIRE(
        std::string_view{diagnostics[0].text} ==
        "SDK initialization failed: application must supply a non-empty 'service' "
        "value in CoreConfig"
    );
  }

  SECTION("M reject config W env is missing") {
    // Given a config struct that's missing an env value
    CoreConfig config("my-client-token", "my-service", "");
    config.SetDiagnosticHandler(diagnostic_handler);

    // When we attempt to create a core from that config
    auto core = Core::Create(config);

    // Then we receive a diagnostic error
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics[0].level == DiagnosticLevel::Error);
    REQUIRE(
        std::string_view{diagnostics[0].text} ==
        "SDK initialization failed: application must supply a non-empty 'env' value in "
        "CoreConfig"
    );
  }
}
