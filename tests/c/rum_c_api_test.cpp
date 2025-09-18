// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "datadog/rum.h"
#include "support/core.hpp"

using namespace datadog;

TEST_CASE("dd_rum null safety", "[unit][rum][c-api]") {
  SECTION("M safely do nothing W target object is null") {
    dd_rum_config_init(nullptr, "my-application-id");
    dd_rum_config_set_application_id(nullptr, "my-application-id");

    REQUIRE(dd_rum_init(nullptr, nullptr) == nullptr);
    dd_rum_destroy(nullptr);

    // TODO(RUM-11367): Validate session functions
    // TODO(RUM-11368): Validate view functions
    // TODO(RUM-11369): Validate action functions
  }
}

TEST_CASE("dd_rum_init", "[unit][rum][c-api]") {
  SECTION("M accept config W dd_rum_config_init was called") {
    // Given a valid core
    dd_core_config_t core_config;
    dd_core_config_init(&core_config, "my-client-token", "my-service", "my-env");
    dd_core_t* core = dd_core_create(&core_config);

    // And a config struct that's been initialized with the bare-minimum set of values
    dd_rum_config config;
    dd_rum_config_init(&config, "my-application-id");

    // When we attempt to register RUM with that config
    dd_rum_t* rum = dd_rum_init(core, &config);

    // Then we get a valid dd_rum_t
    REQUIRE(rum != nullptr);

    // Cleanup
    dd_rum_destroy(rum);
    dd_core_destroy(core);
  }

  SECTION("M accept config W version is 1") {
    // Given a valid core
    dd_core_config_t core_config;
    dd_core_config_init(&core_config, "my-client-token", "my-service", "my-env");
    dd_core_t* core = dd_core_create(&core_config);

    // And a config struct that's been initialized with the bare-minimum set of values
    dd_rum_config config;
    dd_rum_config_init(&config, "my-application-id");

    // When we explicitly set the struct version to 1
    config.version = 1;

    // When we attempt to register RUM with that config
    dd_rum_t* rum = dd_rum_init(core, &config);

    // Then we get a valid dd_rum_t, even in a future where RUM_CONFIG_VERSION has been
    // bumped and is no longer 1
    REQUIRE(rum != nullptr);

    // Cleanup
    dd_rum_destroy(rum);
    dd_core_destroy(core);
  }

  SECTION("M reject config W version not set") {
    // Given a valid core
    dd_core_config_t core_config;
    dd_core_config_init(&core_config, "my-client-token", "my-service", "my-env");
    dd_core_t* core = dd_core_create(&core_config);

    // And a config struct that's just zero-filled
    dd_rum_config config;
    std::memset(&config, 0, sizeof(config));

    // When we attempt to register RUM with that config
    dd_rum_t* rum = dd_rum_init(core, &config);

    // Then we get null
    REQUIRE(rum == nullptr);

    // Cleanup
    dd_core_destroy(core);
  }

  SECTION("M reject config W required value not set") {
    // Given either "" or null as our input value for application_id
    const char* application_ids[2] = {"", nullptr};
    for (const char* application_id : application_ids) {
      // Given a valid core
      dd_core_config_t core_config;
      dd_core_config_init(&core_config, "my-client-token", "my-service", "my-env");
      dd_core_t* core = dd_core_create(&core_config);

      // And a config struct that's initialized with our empty/zero application ID
      dd_rum_config config;
      dd_rum_config_init(&config, application_id);

      // When we attempt to register RUM with that config
      dd_rum_t* rum = dd_rum_init(core, &config);

      // Then we get null
      REQUIRE(rum == nullptr);

      // Cleanup
      dd_core_destroy(core);
    }
  }
}
