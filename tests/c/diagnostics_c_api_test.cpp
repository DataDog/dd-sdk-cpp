// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "datadog/core.h"
#include "datadog/rum.h"

TEST_CASE("dd_core_config diagnostic messages", "[unit][diagnostics][c-api]") {
  // These tests verify that when you register a diagnostic handler callback via the C
  // API, the SDK invokes that callback properly in a variety of different situations,
  // in response to errors occurring in the API layer as well as in the implementation
  // layer. These tests do not intend to exhaustively cover every instance where the SDK
  // might emit a diagnostic message.

  SECTION("M invoke handler callback W API usage error occurs on core init") {
    // Given an SDK config with a missing client token, which should result in a
    // diagnostic error before the Core can be initialized
    dd_core_config_t config;
    dd_core_config_init(&config, "", "my-service", "my-env");

    // And a user-supplied diagnostic message handler callback that will validate the
    // expected message and increment a count so we can verify that it was called
    size_t callback_count = 0;
    auto callback = [](const dd_diagnostic_message_t* message, void* userdata) {
      REQUIRE(userdata);
      size_t& callback_count = *reinterpret_cast<size_t*>(userdata);
      callback_count++;

      REQUIRE(message);
      REQUIRE(message->level == DD_DIAGNOSTIC_LEVEL_ERROR);
      REQUIRE(
          std::string_view{message->text} ==
          "SDK initialization failed: application must supply a non-empty "
          "'client_token' value in dd_core_config_t"
      );
    };
    dd_core_config_set_diagnostic_handler(&config, callback);
    dd_core_config_set_diagnostic_handler_userdata(&config, &callback_count);

    // When we attempt to use our improperly-initialized SDK config
    dd_core_t* core = dd_core_create(&config);
    REQUIRE(!core);

    // Then we get exactly one callback invocation with the expected diagnostic message
    REQUIRE(callback_count == 1);
  }

  SECTION("M invoke handler callback W API usage error occurs on feature init") {
    // Given a valid core config
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");

    // And a RUM config that will cause RUM initialization to fail and produce a
    // diagnostic error
    dd_rum_config_t rum_config;
    dd_rum_config_init(&rum_config, "not-a-valid-uuid");

    // And a handler callback that will increment callback_count and validate that we
    // get the expected error on RUM init
    size_t callback_count = 0;
    auto callback = [](const dd_diagnostic_message_t* message, void* userdata) {
      REQUIRE(userdata);
      size_t& callback_count = *reinterpret_cast<size_t*>(userdata);
      callback_count++;

      REQUIRE(message);
      REQUIRE(message->level == DD_DIAGNOSTIC_LEVEL_ERROR);
      REQUIRE(
          std::string_view{message->text} ==
          "RUM initialization failed: application_id value supplied via "
          "dd_rum_config_t must be a valid, nonzero UUID"
      );
    };
    dd_core_config_set_diagnostic_handler(&config, callback);
    dd_core_config_set_diagnostic_handler_userdata(&config, &callback_count);

    // When we successfully create the core
    dd_core_t* core = dd_core_create(&config);
    REQUIRE(core);
    REQUIRE(callback_count == 0);

    // And then we attempt to register RUM with our invalid config
    dd_rum_t* rum = dd_rum_init(core, &rum_config);
    REQUIRE(!rum);

    // Then we get the expected diagnostic message
    REQUIRE(callback_count == 1);

    // Cleanup
    dd_core_destroy(core);
  }

  SECTION("M invoke handler callback W API usage warning occurs after feature init") {
    // Given a valid core config and a valid RUM config
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");
    dd_rum_config_t rum_config;
    dd_rum_config_init(&rum_config, "a991ca10-4004-4004-4004-beefbeefbeef");

    // And a handler callback that will increment callback_count and validate that we
    // get the expected warning on our call to dd_rum_start_view with bad args
    size_t callback_count = 0;
    auto callback = [](const dd_diagnostic_message_t* message, void* userdata) {
      REQUIRE(userdata);
      size_t& callback_count = *reinterpret_cast<size_t*>(userdata);
      callback_count++;

      REQUIRE(message);
      REQUIRE(message->level == DD_DIAGNOSTIC_LEVEL_WARNING);
      REQUIRE(
          std::string_view{message->text} ==
          "dd_rum_start_view call ignored: application must supply a non-empty view key"
      );
    };
    dd_core_config_set_diagnostic_handler(&config, callback);
    dd_core_config_set_diagnostic_handler_userdata(&config, &callback_count);

    // When we successfully create the core, initialize RUM, and start the SDK
    dd_core_t* core = dd_core_create(&config);
    REQUIRE(core);
    REQUIRE(callback_count == 0);
    dd_rum_t* rum = dd_rum_init(core, &rum_config);
    REQUIRE(rum);
    REQUIRE(callback_count == 0);
    REQUIRE(dd_core_start(core));
    REQUIRE(callback_count == 0);

    // And then we attempt to start a view without specifying a valid key
    dd_rum_start_view(rum, "", "");

    // Then the SDK passes a warning to our diagnostic handler function
    REQUIRE(callback_count == 1);

    // Cleanup
    dd_core_stop(core);
    dd_rum_destroy(rum);
    dd_core_destroy(core);
  }

  SECTION(
      "M not invoke handler callback for warnings W configured threshold is error"
  ) {
    // Given a valid core config and a valid RUM config
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");
    dd_rum_config_t rum_config;
    dd_rum_config_init(&rum_config, "a991ca10-4004-4004-4004-beefbeefbeef");

    // And a handler callback that will increment callback_count when called
    size_t callback_count = 0;
    auto callback = [](const dd_diagnostic_message_t*, void* userdata) {
      REQUIRE(userdata);
      size_t& callback_count = *reinterpret_cast<size_t*>(userdata);
      callback_count++;
    };
    dd_core_config_set_diagnostic_handler(&config, callback);
    dd_core_config_set_diagnostic_handler_userdata(&config, &callback_count);

    // And a configured threshold of 'error', indicating that we don't want to receive
    // debug, status, or warning messages
    dd_core_config_set_diagnostic_threshold(&config, DD_DIAGNOSTIC_LEVEL_ERROR);

    // When we initialize RUM and make the same bad call to dd_rum_start_view
    dd_core_t* core = dd_core_create(&config);
    REQUIRE(core);
    REQUIRE(callback_count == 0);
    dd_rum_t* rum = dd_rum_init(core, &rum_config);
    REQUIRE(rum);
    REQUIRE(callback_count == 0);
    REQUIRE(dd_core_start(core));
    REQUIRE(callback_count == 0);
    dd_rum_start_view(rum, "", "");

    // Then we get no callbacks from the SDK, because a warning does not meet our
    // configured threshold
    REQUIRE(callback_count == 0);

    // Cleanup
    dd_core_stop(core);
    dd_rum_destroy(rum);
    dd_core_destroy(core);
  }

  SECTION("M invoke handler callback W error occurs in implementation layer") {
    // Given a valid core config
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");

    // And a handler callback that will increment callback_count and validate that we
    // get the expected error on SDK start
    size_t callback_count = 0;
    auto callback = [](const dd_diagnostic_message_t* message, void* userdata) {
      REQUIRE(userdata);
      size_t& callback_count = *reinterpret_cast<size_t*>(userdata);
      callback_count++;

      REQUIRE(message);
      REQUIRE(message->level == DD_DIAGNOSTIC_LEVEL_ERROR);
      REQUIRE(
          std::string_view{message->text} ==
          "Failed to start SDK: application must successfully register at least one "
          "feature"
      );
    };
    dd_core_config_set_diagnostic_handler(&config, callback);
    dd_core_config_set_diagnostic_handler_userdata(&config, &callback_count);

    // When we attempt to start the SDK without registering any features
    dd_core_t* core = dd_core_create(&config);
    REQUIRE(core);
    REQUIRE(callback_count == 0);
    const bool started = dd_core_start(core);

    // Then we get a single diagnostic error describing why the core failed to start
    REQUIRE(!started);
    REQUIRE(callback_count == 1);

    // Cleanup
    dd_core_destroy(core);
  }
}
