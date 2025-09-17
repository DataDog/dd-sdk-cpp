// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "datadog/rum.h"
#include "support/core.hpp"

using namespace datadog;

TEST_CASE("dd_rum argument validation", "[unit][rum][c-api]") {
  SECTION("M abort gracefully W target object is null") {
    REQUIRE(dd_rum_init(nullptr, nullptr) == nullptr);
    dd_rum_destroy(nullptr);
  }
}

TEST_CASE("dd_rum_config", "[unit][rum][c-api]") {
  SECTION("M create and destroy config W called") {
    // Given we call dd_rum_config_create
    dd_rum_config_t* config = dd_rum_config_create();

    // Then we get a valid config
    REQUIRE(config != nullptr);

    // When we destroy it
    dd_rum_config_destroy(config);

    // Then no crash occurs
  }

  SECTION("M handle null config W destroy called with null") {
    // When we call destroy with null
    dd_rum_config_destroy(nullptr);

    // Then no crash occurs
  }
}

TEST_CASE("dd_rum_init", "[unit][rum][c-api]") {
  SECTION("M return valid feature W initialized with valid core") {
    // Given a valid dd_core_t
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);

    // When we call dd_rum_init()
    dd_rum_t* rum = dd_rum_init(core, nullptr);

    // Then we get a valid dd_rum_t instance
    REQUIRE(rum);

    // Cleanup
    dd_rum_destroy(rum);
    dd_core_destroy(core);
  }

  SECTION("M return valid feature W initialized with custom config") {
    // Given a valid dd_core_t and a custom config
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_rum_config_t* config = dd_rum_config_create();

    // When we call dd_rum_init() with custom config
    dd_rum_t* rum = dd_rum_init(core, config);

    // Then we get a valid dd_rum_t instance
    REQUIRE(rum);

    // Cleanup
    dd_rum_config_destroy(config);
    dd_rum_destroy(rum);
    dd_core_destroy(core);
  }

  SECTION("M return null W core is null") {
    // Given a null core
    dd_core_t* core = nullptr;

    // When we call dd_rum_init()
    dd_rum_t* rum = dd_rum_init(core, nullptr);

    // Then we get null
    REQUIRE(rum == nullptr);
  }
}

TEST_CASE("dd_rum feature lifecycle", "[unit][rum][c-api]") {
  SECTION("M work properly W feature is registered before core start") {
    // Given a valid dd_core_t and dd_rum_t
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_rum_t* rum = dd_rum_init(core, nullptr);

    // When we start the core
    REQUIRE(dd_core_start(core));

    // Then no errors occur and the core starts successfully
    // (No actual RUM events are generated in this do-nothing implementation)

    // Cleanup
    dd_core_stop(core);
    dd_rum_destroy(rum);
    dd_core_destroy(core);
  }

  SECTION("M handle gracefully W feature registration attempted after core start") {
    // Given a core with a feature already registered
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);

    // Register a feature first so core can start
    dd_rum_t* rum1 = dd_rum_init(core, nullptr);
    REQUIRE(dd_core_start(core));

    // When we try to register another RUM feature after start
    dd_rum_t* rum2 = dd_rum_init(core, nullptr);

    // Then it should fail gracefully (return null) due to improper state
    REQUIRE(rum2 == nullptr);

    // Cleanup
    dd_core_stop(core);
    dd_rum_destroy(rum1);
    dd_core_destroy(core);
  }

  SECTION("M continue normally W rum feature is destroyed prior to core stop") {
    // Given a started core with a RUM feature
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_rum_t* rum = dd_rum_init(core, nullptr);
    dd_core_start(core);

    // When we destroy the dd_rum_t
    dd_rum_destroy(rum);

    // And we stop the core
    dd_core_stop(core);

    // Then this is safe, as the API's references to features are shared: the API
    // can no longer access the feature, but the Core's shared_ptr keeps it alive

    // Cleanup
    dd_core_destroy(core);
  }

  SECTION("M handle gracefully W feature destroyed before core") {
    // Given a valid core and RUM feature
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_rum_t* rum = dd_rum_init(core, nullptr);

    // When we destroy the RUM feature before the core
    dd_rum_destroy(rum);
    dd_core_destroy(core);

    // Then no crashes occur
  }
}
