// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/rum.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "datadog/core.hpp"
#include "features/rum/rum.hpp"
#include "mock/clock.hpp"
#include "support/feature.hpp"

using namespace datadog;
using namespace datadog::impl;

static const CoreConfig CORE_CONFIG("mock-client-token", "mock-service", "mock-env");

TEST_CASE("Rum context population", "[unit][rum]") {
  SECTION("M populate RumFeatureContext W RUM state changes") {
    // Given a valid Rum feature
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    const RumConfig config(*UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef"));
    auto rum = std::make_shared<impl::Rum>(config, clock);

    // And a CoreContext that initially has no RumFeatureContext
    CoreContext context(CORE_CONFIG);
    FeatureTest test(context);
    context = test.GetContextSync();
    REQUIRE(!context.rum);

    // When the SDK first starts
    test.Start(rum);

    // Then our RumFeatureContext should be populated with the configured application
    // ID, along with an ID for the initial session created on init
    context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(
        context.rum->application_id ==
        *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef")
    );
    REQUIRE(context.rum->session_id != UUID::Zero);

    // And our view ID and action ID should be zero
    REQUIRE(context.rum->view_id == UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);

    // TODO(RUM-11368): Test population of view_id in response to StartView
    // TODO(RUM-11368): Test view_id reset in response to StopView
    // TODO(RUM-11368): Test view_id precedence with multiple StartView/StopView calls

    // TODO(RUM-11369): Test population of action_id in response to StartAction
    // TODO(RUM-11369): Test reset of action_id in response to StopAction
    // TODO(RUM-11369): Test action_id precedence with multiple Start/StopAction calls

    // Next: When we call StopSession
    rum->StopSession();

    // Then our RumFeatureContext should contain application_id, but no session_id etc.
    context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(
        context.rum->application_id ==
        *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef")
    );
    REQUIRE(context.rum->session_id == UUID::Zero);
    REQUIRE(context.rum->view_id == UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);

    // Next: When we stop the SDK
    test.Stop(rum);

    // Then our RumFeatureContext should be cleared on shutdown
    context = test.GetContextSync();
    REQUIRE(!context.rum);
  }
}
