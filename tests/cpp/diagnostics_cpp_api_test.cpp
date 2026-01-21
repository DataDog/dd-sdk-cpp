// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "datadog/core.hpp"
#include "datadog/rum.hpp"

using namespace datadog;

TEST_CASE("Core diagnostic messages", "[unit][diagnostics][cpp-api]") {
  // These tests verify that when you register a diagnostic handler callback via the C++
  // API, the SDK invokes that callback properly in a variety of different situations,
  // in response to errors occurring in the API layer as well as in the implementation
  // layer. These tests do not intend to exhaustively cover every instance where the SDK
  // might emit a diagnostic message.

  SECTION("M invoke handler callback W API usage error occurs on core init") {
    // Given an SDK config with a missing client token, which should result in a
    // diagnostic error before the Core can be initialized
    CoreConfig config("", "my-service", "my-env");
    config.SetEventStorageLocation(".");

    // And a user-supplied diagnostic message handler callback that will validate the
    // expected message and increment a count so we can verify that it was called
    size_t callback_count = 0;
    config.SetDiagnosticHandler([&](const DiagnosticMessage& message) {
      callback_count++;
      REQUIRE(message.level == DiagnosticLevel::Error);
      REQUIRE(
          std::string_view{message.text} ==
          "SDK initialization failed: application must supply a non-empty "
          "'client_token' value in CoreConfig"
      );
    });

    // When we attempt to use our improperly-initialized SDK config
    auto core = Core::Create(config);

    // Then we get exactly one callback invocation with the expected diagnostic message
    REQUIRE(callback_count == 1);
  }

  SECTION("M invoke handler callback W API usage error occurs on feature init") {
    // Given a valid core config
    CoreConfig config("my-client-token", "my-service", "my-env");
    config.SetEventStorageLocation(".");

    // And a RUM config that will cause RUM initialization to fail and produce a
    // diagnostic error
    RumConfig rum_config("not-a-valid-uuid");

    // And a handler callback that will increment callback_count and validate that we
    // get the expected error on RUM init
    size_t callback_count = 0;
    config.SetDiagnosticHandler([&](const DiagnosticMessage& message) {
      callback_count++;
      REQUIRE(message.level == DiagnosticLevel::Error);
      REQUIRE(
          std::string_view{message.text} ==
          "RUM initialization failed: application_id value supplied via RumConfig must "
          "be a valid, nonzero UUID"
      );
    });

    // When we successfully create the core
    auto core = Core::Create(config);
    REQUIRE(core);
    REQUIRE(callback_count == 0);

    // And then we attempt to register RUM with our invalid config
    auto rum = Rum::Register(core, rum_config);

    // Then we get the expected diagnostic message
    REQUIRE(callback_count == 1);
  }

  SECTION("M invoke handler callback W API usage warning occurs after feature init") {
    // Given a valid core config and a valid RUM config
    CoreConfig config("my-client-token", "my-service", "my-env");
    config.SetEventStorageLocation(".");
    RumConfig rum_config("a991ca10-4004-4004-4004-beefbeefbeef");

    // And a handler callback that will increment callback_count and validate that we
    // get the expected warning on our call to dd_rum_start_view with bad args
    size_t callback_count = 0;
    config.SetDiagnosticHandler([&](const DiagnosticMessage& message) {
      callback_count++;
      REQUIRE(message.level == DiagnosticLevel::Warning);
      REQUIRE(
          std::string_view{message.text} ==
          "Rum::StartView call ignored: application must supply a non-empty view key"
      );
    });

    // When we successfully create the core, initialize RUM, and start the SDK
    auto core = Core::Create(config);
    REQUIRE(core);
    REQUIRE(callback_count == 0);
    auto rum = Rum::Register(core, rum_config);
    REQUIRE(rum);
    REQUIRE(callback_count == 0);
    REQUIRE(core->Start());
    REQUIRE(callback_count == 0);

    // And then we attempt to start a view without specifying a valid key
    rum->StartView("");

    // Then the SDK passes a warning to our diagnostic handler function
    REQUIRE(callback_count == 1);
    core->Stop();
  }

  SECTION(
      "M not invoke handler callback for warnings W configured threshold is error"
  ) {
    // Given a valid core config and a valid RUM config
    CoreConfig config("my-client-token", "my-service", "my-env");
    config.SetEventStorageLocation(".");
    RumConfig rum_config("a991ca10-4004-4004-4004-beefbeefbeef");

    // And a handler callback that will increment callback_count when called
    size_t callback_count = 0;
    config.SetDiagnosticHandler([&](const DiagnosticMessage&) { callback_count++; });

    // And a configured threshold of 'error', indicating that we don't want to receive
    // debug, status, or warning messages
    config.SetDiagnosticThreshold(DiagnosticLevel::Error);

    // When we initialize RUM and make the same bad call to StartView
    auto core = Core::Create(config);
    REQUIRE(core);
    REQUIRE(callback_count == 0);
    auto rum = Rum::Register(core, rum_config);
    REQUIRE(rum);
    REQUIRE(callback_count == 0);
    REQUIRE(core->Start());
    REQUIRE(callback_count == 0);
    rum->StartView("");

    // Then we get no callbacks from the SDK, because a warning does not meet our
    // configured threshold
    REQUIRE(callback_count == 0);
    core->Stop();
  }

  SECTION("M invoke handler callback W error occurs in implementation layer") {
    // Given a valid core config
    CoreConfig config("my-client-token", "my-service", "my-env");
    config.SetEventStorageLocation(".");

    // And a handler callback that will increment callback_count and validate that we
    // get the expected error on SDK start
    size_t callback_count = 0;
    config.SetDiagnosticHandler([&](const DiagnosticMessage& message) {
      callback_count++;
      REQUIRE(message.level == DiagnosticLevel::Error);
      REQUIRE(
          std::string_view{message.text} ==
          "Failed to start SDK: application must successfully register at least one "
          "feature"
      );
    });

    // When we attempt to start the SDK without registering any features
    auto core = Core::Create(config);
    REQUIRE(core);
    REQUIRE(callback_count == 0);
    const bool started = core->Start();

    // Then we get a single diagnostic error describing why the core failed to start
    REQUIRE(!started);
    REQUIRE(callback_count == 1);
  }
}
