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

static const UUID APPLICATION_ID = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
static const CoreConfig CORE_CONFIG("mock-client-token", "mock-service", "mock-env");
static const RumConfig RUM_CONFIG(APPLICATION_ID);

TEST_CASE("Rum context population", "[unit][rum]") {
  SECTION(
      "M initialize RumFeatureContext with application_id and session_id W SDK starts"
  ) {
    // Given a valid RUM feature
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);

    // And a CoreContext that initially has no RumFeatureContext
    FeatureTest test(CoreContext{CORE_CONFIG});
    CoreContext context = test.GetContextSync();
    REQUIRE(!context.rum);

    // When the SDK first starts
    test.Start(rum);

    // Then our RumFeatureContext should be populated with the configured application
    // ID, along with an ID for the initial session created on init
    context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);

    // And our view ID and action ID should be zero
    REQUIRE(context.rum->view_id == UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);
  }

  SECTION("M clear RumFeatureContext W SDK stops") {
    // Given a running RUM feature which has populated a RumFeatureContext
    MockClock clock;
    FeatureTest test(CoreContext{CORE_CONFIG});
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    test.Start(rum);
    REQUIRE(test.GetContextSync().rum.has_value() == true);

    // When RUM is notified that the SDK is shutting down
    test.Stop(rum);

    // Then the RumFeatureContext is cleared, ensuring that if the SDK is restarted from
    // the same instance, our original state doesn't leak in
    REQUIRE(test.GetContextSync().rum.has_value() == false);
  }

  SECTION("M change session_id W session expires and is replaced") {
    // Given a running RUM feature
    MockClock clock;
    FeatureTest test(CoreContext{CORE_CONFIG});
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    test.Start(rum);

    // And a valid session
    CoreContext context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);
    REQUIRE(context.rum->view_id == UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);
    const UUID initial_session_id = context.rum->session_id;

    // When we wait several hours and start a view, which should cause our original
    // session to be closed and replaced by a new session
    clock.Tick(std::chrono::hours(5));
    rum->StartView("foo");

    // Then our RumFeatureContext is still valid and has the same application_id, but
    // its session ID has changed
    context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);
    REQUIRE(context.rum->session_id != initial_session_id);
  }

  SECTION("M reset session_id W session is explicitly stopped") {
    // Given a running RUM feature with a valid session
    MockClock clock;
    FeatureTest test(CoreContext{CORE_CONFIG});
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    test.Start(rum);

    CoreContext context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);

    // When we call StopSession, which should cause our RUM application scope to no
    // longer have an active session
    rum->StopSession();

    // Then our RumFeatureContext is still valid and has the same application_id, but
    // its session ID is now zero
    context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id == UUID::Zero);
    REQUIRE(context.rum->view_id == UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);
  }

  SECTION("M populate view_id W a view is active") {
    // Given a running RUM feature with a valid session
    MockClock clock;
    FeatureTest test(CoreContext{CORE_CONFIG});
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    test.Start(rum);

    // When we start a view
    rum->StartView("foo");

    // Then our RumFeatureContext has a nonzero view_id
    CoreContext context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);
    REQUIRE(context.rum->view_id != UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);
    const UUID initial_session_id = context.rum->session_id;
    const UUID initial_view_id = context.rum->view_id;

    // Next: When we start a second view
    rum->StartView("bar");

    // Then our RumFeatureContext has the same session_id but a different view_id
    context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id == initial_session_id);
    REQUIRE(context.rum->view_id != UUID::Zero);
    REQUIRE(context.rum->view_id != initial_view_id);
    REQUIRE(context.rum->action_id == UUID::Zero);
  }

  SECTION("M not populate view_id W a view is no longer active") {
    // Given a running RUM feature with a valid session and view
    MockClock clock;
    FeatureTest test(CoreContext{CORE_CONFIG});
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    test.Start(rum);
    rum->StartView("foo");

    // When we explicitly stop the active view
    rum->StopView("foo");

    // Then our RumFeatureContext once again has a view_id of zero, while the session
    // remains intact
    CoreContext context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);
    REQUIRE(context.rum->view_id == UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);
  }

  // TODO(RUM-11369): Test population of action_id in response to StartAction
  // TODO(RUM-11369): Test reset of action_id in response to StopAction
  // TODO(RUM-11369): Test action_id precedence with multiple Start/StopAction calls
}
