// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/rum.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <variant>
#include <vector>

#include "datadog/core.hpp"

#include "datadog/impl/core/context_thread.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/rum/rum.hpp"

#include "mock/clock.hpp"
#include "support/context.hpp"
#include "support/event_writer.hpp"
#include "support/feature.hpp"

using namespace datadog;
using namespace datadog::impl;

static const UUID APPLICATION_ID = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
static const RumConfig RUM_CONFIG(APPLICATION_ID);

TEST_CASE("Rum sampling decision provider follows session lifecycle", "[unit][rum]") {
  constexpr float SAMPLE_RATE = 50.0F;
  MockClock clock;
  clock.FreezeAtMilliseconds(1700000000000);

  auto config = RUM_CONFIG;
  config.SetSessionSampleRate(SAMPLE_RATE);

  CoreContextProvider context_provider(MOCK_CONTEXT);
  Queue<std::function<void()>> context_queue;
  DiagnosticLogger diagnostic_logger;
  std::vector<RumSessionState> session_states;

  auto sampling_decision_provider = std::make_shared<SamplingDecisionProvider>();
  auto rum = std::make_shared<impl::Rum>(config, clock, sampling_decision_provider);

  // Before Rum starting, the decision provider has no session snapshot.
  REQUIRE(!sampling_decision_provider->GetSessionSnapshot());

  // Start Rum. Published messages of the type that matters to us
  // (RumSessionStateChangedMessage) will be stored in the session_states vector.
  // However, the context queue is frozen.
  rum->OnCoreStarted(
      FeatureScope::Create(
          context_provider,
          MakeTestEventWriter(),
          [&](FeatureMessage message) {
            if (const auto* state =
                    std::get_if<RumSessionStateChangedMessage>(&message)) {
              session_states.push_back(state->session_state);
            }
            return true;
          },
          diagnostic_logger,
          context_queue
      )
  );

  // Nothing in the contex queue ran yet, so the vector is empty…
  REQUIRE(session_states.empty());

  // … but since it's done synchronously, we already have the data in the decision
  // provider for the initial session.
  const auto snapshot = sampling_decision_provider->GetSessionSnapshot();
  REQUIRE(snapshot);
  REQUIRE(snapshot->session_id != UUID::Zero);
  const DeterministicSampler expected_sampler(snapshot->session_id, SAMPLE_RATE);
  REQUIRE(snapshot->sampler.Sample() == expected_sampler.Sample());
  REQUIRE(snapshot->sampler.GetRulePSR() == expected_sampler.GetRulePSR());

  auto process_commands = [&]() {
    // This will essentially run the context queue and append all the queued
    // session_states into our test vector.
    rum->OnCoreStopRequested();
    context_queue.Stop();
    std::thread context_thread{
        ContextThreadMain, std::ref(diagnostic_logger), std::ref(context_queue)
    };
    context_thread.join();
    rum->OnCoreStopping();
  };

  SECTION("M preserve initial snapshot W SDKInit is processed") {
    process_commands();
    REQUIRE(session_states.size() == 1);
  }

  SECTION("M update snapshot W session is stopped and restarted") {
    rum->StopSession();
    rum->StartView("foo");
    process_commands();

    REQUIRE(session_states.size() == 3);
    const RumSessionState& stopped_session = session_states[1];
    REQUIRE(stopped_session.session_id == snapshot->session_id);
    REQUIRE(!stopped_session.is_active);

    const RumSessionState& next_session = session_states[2];
    REQUIRE(next_session.session_id != UUID::Zero);
    REQUIRE(next_session.session_id != snapshot->session_id);
    REQUIRE(next_session.is_active);
    REQUIRE(!next_session.is_initial_session);
  }

  const RumSessionState& initial_session = session_states.front();
  REQUIRE(initial_session.session_id == snapshot->session_id);
  REQUIRE(initial_session.is_sampled == snapshot->sampler.Sample());
  REQUIRE(initial_session.is_active);
  REQUIRE(initial_session.is_initial_session);

  const auto current_snapshot = sampling_decision_provider->GetSessionSnapshot();
  REQUIRE(current_snapshot);
  REQUIRE(current_snapshot->session_id == session_states.back().session_id);
  REQUIRE(current_snapshot->sampler.Sample() == session_states.back().is_sampled);
  const DeterministicSampler expected_current_sampler(
      current_snapshot->session_id, SAMPLE_RATE
  );
  REQUIRE(current_snapshot->sampler.Sample() == expected_current_sampler.Sample());
  REQUIRE(
      current_snapshot->sampler.GetRulePSR() == expected_current_sampler.GetRulePSR()
  );
}

TEST_CASE("Rum context population", "[unit][rum]") {
  SECTION(
      "M initialize RumFeatureContext with application_id and session_id W SDK starts"
  ) {
    // Given a valid RUM feature
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );

    // And a CoreContext that initially has no RumFeatureContext
    FeatureTest test(MOCK_CONTEXT);
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

  SECTION("M clear RumFeatureContext W SDK restarts") {
    // Given a RUM feature that has been started, producing a session
    MockClock clock;
    FeatureTest test(MOCK_CONTEXT);
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );
    test.Start(rum);
    REQUIRE(test.GetContextSync().rum.has_value() == true);
    const UUID initial_session_id = test.GetContextSync().rum->session_id;
    REQUIRE(initial_session_id != UUID::Zero);

    // When the SDK is stopped and then restarted
    test.Stop(rum);
    test.Start(rum);

    // Then the RumFeatureContext reflects a fresh new session, not the stale state
    // from the previous run
    CoreContext context = test.GetContextSync();
    REQUIRE(context.rum.has_value() == true);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);
    REQUIRE(context.rum->session_id != initial_session_id);
  }

  SECTION("M change session_id W session expires and is replaced") {
    // Given a running RUM feature
    MockClock clock;
    FeatureTest test(MOCK_CONTEXT);
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );
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
    FeatureTest test(MOCK_CONTEXT);
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );
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
    FeatureTest test(MOCK_CONTEXT);
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );
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
    FeatureTest test(MOCK_CONTEXT);
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );
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

  SECTION("M populate action_id W an action is active") {
    // Given a running RUM feature with a valid session
    MockClock clock;
    FeatureTest test(MOCK_CONTEXT);
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );
    test.Start(rum);

    // When we start a view and then start an action within that view
    rum->StartView("foo");
    rum->StartAction(RumActionType::Custom, "long-custom");

    // Then our RumFeatureContext has a nonzero action_id
    CoreContext context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);
    REQUIRE(context.rum->view_id != UUID::Zero);
    REQUIRE(context.rum->action_id != UUID::Zero);
  }

  SECTION("M not populate action_id W action is no longer active") {
    // Given a running RUM feature with a valid session and view
    MockClock clock;
    FeatureTest test(MOCK_CONTEXT);
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );
    test.Start(rum);

    // When we start a view, start an action within that view, then stop that action
    rum->StartView("foo");
    rum->StartAction(RumActionType::Custom, "long-custom");
    rum->StopAction("long-custom");

    // Then our RumFeatureContext has an action_id of zero
    CoreContext context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);
    REQUIRE(context.rum->view_id != UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);
  }

  SECTION("M not populate action_id W view is no longer active") {
    // Given a running RUM feature with a valid session and view
    MockClock clock;
    FeatureTest test(MOCK_CONTEXT);
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );
    test.Start(rum);

    // When we start a view, start an action within that view, then stop the view
    rum->StartView("foo");
    rum->StartAction(RumActionType::Custom, "long-custom");
    rum->StopView("foo");

    // Then our RumFeatureContext has both a view_id and an action_id of zero
    CoreContext context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id != UUID::Zero);
    REQUIRE(context.rum->view_id == UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);
  }

  SECTION("M not populate action_id W session is no longer active") {
    // Given a running RUM feature with a valid session and view
    MockClock clock;
    FeatureTest test(MOCK_CONTEXT);
    clock.FreezeAtMilliseconds(1700000000000);
    auto rum = std::make_shared<impl::Rum>(
        RUM_CONFIG, clock, std::weak_ptr<SamplingDecisionProvider>{}
    );
    test.Start(rum);

    // When we start a view, start an action within that view, then stop the session
    rum->StartView("foo");
    rum->StartAction(RumActionType::Custom, "long-custom");
    rum->StopSession();

    // Then our RumFeatureContext has no session_id, view_id, or action_id
    CoreContext context = test.GetContextSync();
    REQUIRE(context.rum);
    REQUIRE(context.rum->application_id == APPLICATION_ID);
    REQUIRE(context.rum->session_id == UUID::Zero);
    REQUIRE(context.rum->view_id == UUID::Zero);
    REQUIRE(context.rum->action_id == UUID::Zero);
  }
}
