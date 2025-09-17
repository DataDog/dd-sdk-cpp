// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <thread>
#include <vector>

#include "datadog/core.h"

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
  SECTION("M accept config W dd_core_config_init was called") {
    // Given a config struct that's been initialized with the bare-minimum set of values
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get a valid dd_core_t
    REQUIRE(core != nullptr);
    dd_core_destroy(core);
  }

  SECTION("M accept config W version is 1") {
    // Given a properly-initialized config struct
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");

    // When we explicitly set the struct version to 1
    config.version = 1;

    // And we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get a valid dd_core_t, even in a future where CORE_CONFIG_VERSION has
    // been bumped and is no longer 1
    REQUIRE(core != nullptr);
    dd_core_destroy(core);
  }

  SECTION("M reject config W version not set") {
    // Given a config struct that's just zero-filled
    dd_core_config_t config;
    std::memset(&config, 0, sizeof(config));

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get null
    REQUIRE(core == nullptr);
  }

  SECTION("M reject config W required values missing") {
    for (int i = 0; i < 3; i++) {
      // Given three required values, one of which is null or empty
      const char* client_token = i == 0 ? nullptr : "my-client-token";
      const char* service = i == 1 ? "" : "my-service";
      const char* env = i == 2 ? nullptr : "my-env";

      // And a config that's initialized from those values
      dd_core_config_t config;
      dd_core_config_init(&config, client_token, service, env);

      // When we attempt to create a core from that config
      dd_core_t* core = dd_core_create(&config);

      // Then we get null
      REQUIRE(core == nullptr);
    }
  }
}
