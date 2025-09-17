// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "datadog/rum.hpp"
#include "support/core.hpp"

using namespace datadog;

TEST_CASE("Rum::Register", "[unit][rum][cpp-api]") {
  SECTION("M return valid feature W registered with core") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // When we register the RUM feature
    auto rum = Rum::Register(*core);

    // Then we get a valid Rum interface
    REQUIRE(rum != nullptr);
  }

  SECTION("M return valid feature W registered with custom config") {
    // Given a valid core and custom config
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);
    RumConfig config;

    // When we register the RUM feature with custom config
    auto rum = Rum::Register(*core, config);

    // Then we get a valid Rum interface
    REQUIRE(rum != nullptr);
  }

  SECTION("M return valid feature W registered with default config") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // When we register the RUM feature with implicit default config
    auto rum = Rum::Register(*core);

    // Then we get a valid Rum interface
    REQUIRE(rum != nullptr);
  }
}

TEST_CASE("Rum feature lifecycle", "[unit][rum][cpp-api]") {
  SECTION("M work properly W feature is registered before core start") {
    // Given a valid core and RUM feature
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);
    auto rum = Rum::Register(*core);

    // When we start the core
    REQUIRE(core->Start());

    // Then no errors occur and the core starts successfully
    // (No actual RUM events are generated in this do-nothing implementation)

    // Cleanup
    core->Stop();
  }

  SECTION("M handle gracefully W feature registration attempted after core start") {
    // Given a core with a feature already registered
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // Register a feature first so core can start
    auto rum1 = Rum::Register(*core);
    REQUIRE(core->Start());

    // When we try to register another RUM feature after start
    auto rum2 = Rum::Register(*core);

    // Then it should fail gracefully (return null) due to improper state
    REQUIRE(rum2 == nullptr);

    // Cleanup
    core->Stop();
  }

  SECTION("M continue normally W rum feature is destroyed prior to core stop") {
    // Given a started core with a RUM feature
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);
    auto rum = Rum::Register(*core);
    core->Start();

    // When we reset the RUM feature reference
    rum.reset();

    // And we stop the core
    core->Stop();

    // Then this is safe, as the API's references to features are shared: the API
    // can no longer access the feature, but the Core's shared_ptr keeps it alive
  }

  SECTION("M handle gracefully W feature destroyed before core") {
    std::shared_ptr<Rum> rum;
    {
      // Given a valid core and RUM feature
      auto test = CoreTestHarness::Init();
      auto core = CoreTestHarness::WrapForCpp(test);
      rum = Rum::Register(*core);

      // When the core goes out of scope but the RUM feature reference remains
    }

    // Then the RUM feature reference is still safe to hold (though not functional)
    REQUIRE(rum != nullptr);
  }

  SECTION("M handle gracefully W registered multiple times") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // When we register the RUM feature multiple times
    auto rum1 = Rum::Register(*core);
    auto rum2 = Rum::Register(*core);

    // Then the first registration succeeds
    REQUIRE(rum1 != nullptr);

    // And the second registration may return null or the same instance
    // (implementation-specific behavior for duplicate feature registration)
    // Both behaviors are acceptable for this do-nothing API
    (void)rum2;  // Don't require specific behavior for duplicate registration
  }
}

TEST_CASE("RumConfig", "[unit][rum][cpp-api]") {
  SECTION("M construct with default values W default constructor called") {
    // Given we construct a RumConfig
    RumConfig config;

    // Then no errors occur (basic construction test)
    // The config has no public members to test in this do-nothing implementation
    (void)config;  // Suppress unused variable warning
  }

  SECTION("M copy construct W copy constructor called") {
    // Given an existing config
    RumConfig original;

    // When we copy construct from it
    RumConfig copy(original);

    // Then no errors occur (basic copy construction test)
    (void)copy;  // Suppress unused variable warning
  }

  SECTION("M assign W assignment operator called") {
    // Given two configs
    RumConfig config1;
    RumConfig config2;

    // When we assign one to the other
    config2 = config1;

    // Then no errors occur (basic assignment test)
  }
}
