// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <functional>
#include <thread>
#include <vector>

#include "datadog/core.h"
#include "support/catch.hpp"

TEST_CASE("dd_core null safety", "[unit][core][c-api]") {
  SECTION("M safely do nothing W target object is null") {
    REQUIRE(dd_core_create(nullptr) == nullptr);
    dd_core_destroy(nullptr);

    dd_core_set_tracking_consent(nullptr, DD_TRACKING_CONSENT_GRANTED);
    REQUIRE(dd_core_start(nullptr) == false);
    dd_core_stop(nullptr);
  }
}

TEST_CASE("dd_core_config validation", "[unit][core][c-api]") {
  // Capture diagnostic messages that would be printed to stderr by default
  std::vector<dd_diagnostic_message_t> diagnostics;
  auto handle_diagnostic = [](const dd_diagnostic_message_t* message, void* userdata) {
    auto vec_ptr = reinterpret_cast<std::vector<dd_diagnostic_message_t>*>(userdata);
    vec_ptr->push_back(*message);
  };
  auto configure_diagnostic_handler = [&](dd_core_config_t* config) {
    dd_core_config_set_diagnostic_handler(config, handle_diagnostic);
    dd_core_config_set_diagnostic_handler_userdata(config, &diagnostics);
  };

  SECTION("M accept config W dd_core_config_init was called") {
    // Given a config struct that's been initialized with the bare-minimum set of values
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");
    configure_diagnostic_handler(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get a valid dd_core_t
    REQUIRE(core != nullptr);
    dd_core_destroy(core);

    // And no diagnostic warnings/errors are emitted
    REQUIRE(diagnostics.empty());
  }

  SECTION("M accept config W version is 1") {
    // Given a properly-initialized config struct
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");
    configure_diagnostic_handler(&config);

    // When we explicitly set the struct version to 1
    config.version = 1;

    // And we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get a valid dd_core_t, even in a future where CORE_CONFIG_VERSION has
    // been bumped and is no longer 1
    REQUIRE(core != nullptr);
    dd_core_destroy(core);
    REQUIRE(diagnostics.empty());
  }

  SECTION("M reject config W version not set") {
    // Given a config struct that's just zero-filled
    dd_core_config_t config;
    std::memset(&config, 0, sizeof(config));
    configure_diagnostic_handler(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get null
    REQUIRE(core == nullptr);

    // And we receive no diagnostic messages: our handler callback is stored in the
    // config struct, which the SDK can't be sure was properly initialized
    REQUIRE(diagnostics.empty());
  }

  SECTION("M reject config W client_token is missing") {
    // Given a config that's missing a client_token value
    auto client_token = GENERATE("", nullptr);
    dd_core_config_t config;
    dd_core_config_init(&config, client_token, "my-service", "my-env");
    configure_diagnostic_handler(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get null
    REQUIRE(core == nullptr);

    // And we receive a diagnostic error
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics[0].level == DD_DIAGNOSTIC_LEVEL_ERROR);
    REQUIRE(
        std::string_view{diagnostics[0].text} ==
        "SDK initialization failed: application must supply a non-empty 'client_token' "
        "value in dd_core_config_t"
    );
  }

  SECTION("M reject config W service is missing") {
    // Given a config that's missing a service value
    auto service = GENERATE("", nullptr);
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", service, "my-env");
    configure_diagnostic_handler(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get null
    REQUIRE(core == nullptr);

    // And we receive a diagnostic error
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics[0].level == DD_DIAGNOSTIC_LEVEL_ERROR);
    REQUIRE(
        std::string_view{diagnostics[0].text} ==
        "SDK initialization failed: application must supply a non-empty 'service' "
        "value in dd_core_config_t"
    );
  }

  SECTION("M reject config W env is missing") {
    // Given a config that's missing an env value
    auto env = GENERATE("", nullptr);
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", env);
    configure_diagnostic_handler(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get null
    REQUIRE(core == nullptr);

    // And we receive a diagnostic error
    REQUIRE(diagnostics.size() == 1);
    REQUIRE(diagnostics[0].level == DD_DIAGNOSTIC_LEVEL_ERROR);
    REQUIRE(
        std::string_view{diagnostics[0].text} ==
        "SDK initialization failed: application must supply a non-empty 'env' value in "
        "dd_core_config_t"
    );
  }
}
