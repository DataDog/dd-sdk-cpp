// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "datadog/rum.hpp"
#include "support/core.hpp"

using namespace datadog;

TEST_CASE("Rum null safety", "[unit][rum][cpp-api]") {
  SECTION("M safely do nothing W this wraps nullptr") {
    // Given both a valid Core interface that has no valid implementation pointer, as
    // well as a straight-up null pointer to a Core interface
    const datadog::CoreConfig invalid_config("", "", "");
    std::shared_ptr<Core> noop_core = Core::Create(invalid_config);
    std::shared_ptr<Core> null_core;
    std::vector<std::shared_ptr<Core>> cores = {noop_core, null_core};

    // And a valid RUM Config
    const datadog::RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");

    for (std::shared_ptr<Core>& core : cores) {
      // When we register the RUM feature on an invalid core
      auto rum = Rum::Register(core, config);

      // Then we get a valid object that handles all member function calls as a no-op
      REQUIRE(rum != nullptr);
      rum->SetAttribute("foo", Attribute::Int(100));
      rum->DeleteAttribute("foo");

      // TODO(RUM-11367): Validate session member functions
      // TODO(RUM-11368): Validate view member functions
      // TODO(RUM-11369): Validate action member functions
    }
  }
}

TEST_CASE("Rum::Register", "[unit][rum][cpp-api]") {
  SECTION("M accept config W all required values are present") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // And a valid Rum config
    RumConfig config("a991ca10-4004-4004-4004-beefbeefbeef");

    // When we register the RUM feature
    auto rum = Rum::Register(core, config);

    // Then we get a valid Rum interface
    REQUIRE(rum != nullptr);
  }

  SECTION("M reject config W required value not set") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // And a Rum config that lacks a valid application ID
    RumConfig config("this-is-not-a-valid-uuid-so-application-id-will-default-to-zero");

    // When we register the RUM feature
    auto rum = Rum::Register(core, config);

    // Then we get a valid pointer to a no-op Rum interface
    // TODO: Surface some indication of whether a call to the C++ API succeeded (and
    // gave you a valid, functional object) or failed (and gave you a no-op interface)
    REQUIRE(rum != nullptr);
  }
}

// TODO(RUM-11368): Validate that view functions result in the expected events
// TODO(RUM-11368): Validate that events include global attributes values
// TODO(RUM-11369): Validate that action functions result in the expected events
