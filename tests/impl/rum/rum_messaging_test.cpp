// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <chrono>
#include <memory>

#include "datadog/core.hpp"
#include "datadog/rum.hpp"

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/rum/rum.hpp"

#include "mock/clock.hpp"
#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/feature.hpp"

using namespace datadog;
using namespace datadog::impl;

static const UUID APPLICATION_ID = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
static const RumConfig RUM_CONFIG(APPLICATION_ID);

/**
 * Given a vector of accumulated FeatureMessage values, returns the total number of of
 * messages that match the given type T.
 */
template <typename T>
static size_t CountMessages(const std::vector<FeatureMessage>& msgs) {
  return static_cast<size_t>(
      std::count_if(msgs.begin(), msgs.end(), [](const FeatureMessage& m) {
        return std::holds_alternative<T>(m);
      })
  );
}

/**
 * Given a vector of accumulated FeatureMessage values, returns the last message in the
 * vector that matches the given type T, or nullptr if no messages of that type were
 * published during the test.
 */
template <typename T>
static const T* FindLastMessage(const std::vector<FeatureMessage>& msgs) {
  const T* result = nullptr;
  for (const auto& m : msgs) {
    if (const T* p = std::get_if<T>(&m)) {
      result = p;
    }
  }
  return result;
}

TEST_CASE("Rum messaging", "[unit][rum]") {
  MockClock clock;
  clock.FreezeAtMilliseconds(1700000000000);

  // === RumSessionStateChangedMessage ===

  SECTION(
      "M emit RumSessionStateChangedMessage with is_initial_session=true and "
      "is_active=true W SDK starts"
  ) {
    // Given a Rum feature with default config (incl. session sample rate of 100%)
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK, causing an SDKInit command to be processed
    test.Start(rum);

    // Then RUM publishes a RumSessionStateChangedMessage with a valid session_id,
    // describing a session that is active and sampled
    const auto* msg =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(msg != nullptr);
    REQUIRE(msg->session_state.session_id != UUID::Zero);
    REQUIRE(msg->session_state.is_sampled == true);
    REQUIRE(msg->session_state.is_active == true);

    // And that session is the the very first session created by RUM
    REQUIRE(msg->session_state.is_initial_session == true);

    // And no explicitly-named views have been tracked in that session yet
    REQUIRE(msg->session_state.has_tracked_any_view == false);
  }

  SECTION(
      "M emit RumSessionStateChangedMessage with is_sampled=false W new session is "
      "created but excluded from sampling"
  ) {
    // Given a Rum feature with a session sample rate of 0%
    auto rum_config = RUM_CONFIG;
    rum_config.SetSessionSampleRate(0.0f);
    auto rum = std::make_shared<impl::Rum>(rum_config, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK, causing an SDKInit command to be processed, creating an
    // initial session that will inevitably not be sampled
    test.Start(rum);

    // Then RUM publishes a RumSessionStateChangedMessage with a valid session_id,
    // describing a session that is active and in a valid initial state
    const auto* msg =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(msg != nullptr);
    REQUIRE(msg->session_state.session_id != UUID::Zero);
    REQUIRE(msg->session_state.is_active == true);
    REQUIRE(msg->session_state.is_initial_session == true);
    REQUIRE(msg->session_state.has_tracked_any_view == false);

    // But the value shows that the session has not been sampled
    REQUIRE(msg->session_state.is_sampled == false);
  }

  SECTION(
      "M emit RumSessionStateChangedMessage with same session_id and is_active=false "
      "W StopSession() is called"
  ) {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK and get an initial RumSessionStateChangedMessage
    test.Start(rum);
    const auto* initial =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(initial != nullptr);
    const UUID initial_session_id = initial->session_state.session_id;
    REQUIRE(initial->session_state.is_active == true);

    // And then we call StopSession to explicitly stop that session
    rum->StopSession();

    // Then RUM publishes another RumSessionStateChangedMessage that describes the same
    // session, but now indicates that it's no longer active
    const auto* stopped =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(stopped != nullptr);
    REQUIRE(stopped->session_state.session_id == initial_session_id);
    REQUIRE(stopped->session_state.is_active == false);
  }

  SECTION(
      "M emit RumSessionStateChangedMessage with is_active=false W StopSession() "
      "is called after inactivity timeout"
  ) {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK and record our initial session ID
    test.Start(rum);
    const auto* initial =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(initial != nullptr);
    const UUID initial_session_id = initial->session_state.session_id;
    REQUIRE(initial->session_state.is_active == true);

    // And the inactivity timeout elapses without any user activity
    clock.Tick(std::chrono::hours(1));

    // And then we call StopSession, explicitly requesting that no further events be
    // sent until the user resumes activity
    rum->StopSession();

    // Then RUM should publish a RumSessionStateChangedMessage indicating that the
    // session is no longer active, regardless of whether the inactivity timeout was
    // detected before or at the time of the StopSession call
    const auto* latest =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(latest != nullptr);
    REQUIRE(latest->session_state.session_id == initial_session_id);
    REQUIRE(latest->session_state.is_active == false);
  }

  SECTION(
      "M emit RumSessionStateChangedMessage with has_tracked_any_view=true W "
      "StartView() is called during session lifetime"
  ) {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK and get an initial RumSessionStateChangedMessage
    test.Start(rum);
    const auto* initial =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(initial != nullptr);
    const UUID initial_session_id = initial->session_state.session_id;
    REQUIRE(initial->session_state.has_tracked_any_view == false);

    // And then we call StartView to track a foreground view in that session
    rum->StartView("foo");

    // Then RUM publishes another RumSessionStateChangedMessage that describes the same
    // session, but now indicates that it has tracked a foreground view
    const auto* latest =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(latest != nullptr);
    REQUIRE(latest->session_state.session_id == initial_session_id);
    REQUIRE(latest->session_state.is_active == true);
  }

  SECTION(
      "M emit RumSessionStateChangedMessage with new session_id, "
      "is_initial_session=false W session refresh occurs"
  ) {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK and get an initial RumSessionStateChangedMessage
    test.Start(rum);
    const auto* initial =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(initial != nullptr);
    const UUID initial_session_id = initial->session_state.session_id;
    REQUIRE(initial->session_state.has_tracked_any_view == false);

    // And then 1 hour elapses, exceeding our ~15m session inactivity timeout
    clock.Tick(std::chrono::hours(1));

    // And then we call StartView to track a foreground view, causing a new session to
    // be created to succeed our now-expired initial session
    rum->StartView("foo");

    // Then RUM publishes another RumSessionStateChangedMessage that describes a new
    // session, also indicating that it has tracked a foreground view since it was
    // created in response to StartView
    const auto* latest =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(latest != nullptr);
    REQUIRE(latest->session_state.session_id != initial_session_id);
    REQUIRE(latest->session_state.is_sampled == true);
    REQUIRE(latest->session_state.is_active == true);
    REQUIRE(latest->session_state.has_tracked_any_view == true);

    // And our new session is not flagged as the very first session, since it was
    // created to succeed an earlier session and not in response to SDK init
    REQUIRE(latest->session_state.is_initial_session == false);
  }

  SECTION(
      "M not emit a redundant RumSessionStateChangedMessage W session state is "
      "unchanged"
  ) {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK, then start a view in our initial session
    test.Start(rum);
    rum->StartView("foo");

    // Then we should end up with two messages: one describing our initial session state
    // in which no view existed, and another where has_tracked_any_view is now true
    REQUIRE(CountMessages<RumSessionStateChangedMessage>(test.feature_messages) == 2);
    REQUIRE(
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages)
            ->session_state.has_tracked_any_view == true
    );

    // Next: When we stop 'foo' and start a new view called 'bar'
    rum->StopView("foo");
    rum->StartView("bar");

    // Then neither of those RUM state changes causes a new session state message to be
    // emitted, as session state has no meaningfully changed
    REQUIRE(CountMessages<RumSessionStateChangedMessage>(test.feature_messages) == 2);
  }

  // === RumViewEventGeneratedMessage ===

  SECTION("M emit RumViewEventGeneratedMessage W StartView() causes a view event") {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK and then track a view called 'foo'
    test.Start(rum);
    rum->StartView("foo");

    // Then Rum produces a single RumViewEventGeneratedMessage that encodes the view
    // event produced to describe our new view
    REQUIRE(CountMessages<RumViewEventGeneratedMessage>(test.feature_messages) == 1);
    const auto* msg =
        FindLastMessage<RumViewEventGeneratedMessage>(test.feature_messages);
    REQUIRE(msg != nullptr);
    REQUIRE(msg->view_event.view.url == "foo");
    REQUIRE(msg->view_event.view.is_active.value == true);
  }

  SECTION(
      "M emit RumViewResetMessage W StopView() is called with no subsequent StartView()"
  ) {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK, track a view called 'foo', and then explicitly stop that
    // same view
    test.Start(rum);
    rum->StartView("foo");
    rum->StopView("foo");

    // Then Rum produces two RumViewEventGeneratedMessages, since the StopView call
    // results in a final view event with is_active = false
    REQUIRE(CountMessages<RumViewEventGeneratedMessage>(test.feature_messages) == 2);
    const auto* last_view_msg =
        FindLastMessage<RumViewEventGeneratedMessage>(test.feature_messages);
    REQUIRE(last_view_msg->view_event.view.url == "foo");
    REQUIRE(last_view_msg->view_event.view.is_active.value == false);

    // And Rum also produces a RumViewResetMessage to indicate that there is no longer
    // an active view
    REQUIRE(CountMessages<RumViewResetMessage>(test.feature_messages) == 1);

    // And that RumViewResetMessage is sent _after_ the corresponding
    // RumViewEventGeneratedMessage
    std::holds_alternative<RumViewResetMessage>(test.feature_messages.back());
  }

  SECTION(
      "M not emit RumViewResetMessage W StartView() immediately follows StopView()"
  ) {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK, track a view called 'foo', and then explicitly start
    // another view called 'bar' that replaces 'foo'
    test.Start(rum);
    rum->StartView("foo");
    rum->StartView("bar");

    // Then Rum produces three view events - one to start 'foo', one to end 'foo', and
    // another to start 'bar' - but those last two events occur in the course of
    // processing a single command, and Rum only broadcasts a
    // RumViewEventGeneratedMessage for the most recently-produced view event
    REQUIRE(CountMessages<RumViewEventGeneratedMessage>(test.feature_messages) == 2);
    const auto* last_view_msg =
        FindLastMessage<RumViewEventGeneratedMessage>(test.feature_messages);
    REQUIRE(last_view_msg->view_event.view.url == "bar");
    REQUIRE(last_view_msg->view_event.view.is_active.value == true);

    // And Rum does not produce a RumViewResetMessage, since the session has immediately
    // transitioned from one view to another, without remaining in a no-active-view
    // state
    REQUIRE(CountMessages<RumViewResetMessage>(test.feature_messages) == 0);
  }

  SECTION(
      "M not emit RumViewEventGeneratedMessage for an inactive view W a resource "
      "completes on it"
  ) {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK, track 'foo', and start a resource on it before switching
    // to a new view 'bar' while the resource is still in flight
    test.Start(rum);
    rum->StartView("foo");
    rum->StartResource(
        "r1", RumRequestDetails{RumResourceMethod::Get, "https://example.com"}
    );
    rum->StartView("bar");

    // At this point we expect two broadcasts: one for 'foo' (its initial event) and
    // one for 'bar' (the last event captured within the StartView("bar") command,
    // which wins over 'foo's final event in the same command)
    REQUIRE(CountMessages<RumViewEventGeneratedMessage>(test.feature_messages) == 2);
    REQUIRE(
        FindLastMessage<RumViewEventGeneratedMessage>(test.feature_messages)
            ->view_event.view.url == "bar"
    );

    // When the resource that was started on the now-inactive 'foo' view completes
    rum->StopResource("r1", RumResponseDetails{200});

    // Then no new RumViewEventGeneratedMessage should be broadcast: the stale 'foo'
    // view event generated by the resource completion must not overwrite the broadcast
    // context that was set for the active 'bar' view
    REQUIRE(CountMessages<RumViewEventGeneratedMessage>(test.feature_messages) == 2);
    REQUIRE(
        FindLastMessage<RumViewEventGeneratedMessage>(test.feature_messages)
            ->view_event.view.url == "bar"
    );
  }

  SECTION(
      "M emit RumViewResetMessage W session refresh occurs without generating a new "
      "view event"
  ) {
    // Given a Rum feature with default config
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // When we start the SDK and create an initial view called 'foo'
    test.Start(rum);
    rum->StartView("foo");

    // And then 1 hour elapses, exceeding our ~15m session inactivity timeout
    clock.Tick(std::chrono::hours(1));

    // And then we call StopView, causing a session refresh to occur, without creating a
    // new view within that session ('foo' will not be implicitly carried over to the
    // new session since the refresh was triggered by an explicit request to end
    // tracking of that view)
    rum->StopView("foo");

    // Then Rum only ever produces a single view event: it does not have the chance to
    // update 'foo' with view.is_active = false since the change in view state occurs
    // after the session is already expired
    REQUIRE(CountMessages<RumViewEventGeneratedMessage>(test.feature_messages) == 1);
    const auto* last_view_msg =
        FindLastMessage<RumViewEventGeneratedMessage>(test.feature_messages);
    REQUIRE(last_view_msg->view_event.view.url == "foo");
    REQUIRE(last_view_msg->view_event.view.is_active.value == true);

    // But nevertheless, Rum _does_ produce a RumViewResetMessage to indicate that it
    // no longer has an active view
    REQUIRE(CountMessages<RumViewResetMessage>(test.feature_messages) == 1);

    // And our latest session state looks the way we expect it to for a refreshed
    // session with no views
    const auto* latest =
        FindLastMessage<RumSessionStateChangedMessage>(test.feature_messages);
    REQUIRE(latest != nullptr);
    REQUIRE(latest->session_state.session_id != UUID::Zero);
    REQUIRE(latest->session_state.is_sampled == true);
    REQUIRE(latest->session_state.is_active == true);
    REQUIRE(latest->session_state.is_initial_session == false);
    REQUIRE(latest->session_state.has_tracked_any_view == false);
  }

  // === RumGlobalAttributesChangedMessage ===

  SECTION("M emit RumGlobalAttributesChangedMessage W AddAttribute() is called") {
    // Given a started Rum feature
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);
    test.Start(rum);

    // And an initial message indicating that no custom attributes are present
    REQUIRE(
        CountMessages<RumGlobalAttributesChangedMessage>(test.feature_messages) == 1
    );
    REQUIRE(
        FindLastMessage<RumGlobalAttributesChangedMessage>(test.feature_messages)
            ->attributes.GetObjectPropertyCount() == 0
    );

    // When we call AddAttribute
    rum->AddAttribute("key", Attribute::String("value"));

    // Then we get another RumGlobalAttributeChangedMessage containing our new value
    REQUIRE(
        CountMessages<RumGlobalAttributesChangedMessage>(test.feature_messages) == 2
    );
    const auto* msg =
        FindLastMessage<RumGlobalAttributesChangedMessage>(test.feature_messages);
    REQUIRE(msg->attributes.GetObjectPropertyCount() == 1);
    REQUIRE(msg->attributes.GetObjectProperty("key").GetStringValue() == "value");
  }

  SECTION("M emit RumGlobalAttributesChangedMessage W RemoveAttribute() is called") {
    // Given a started Rum feature
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);
    test.Start(rum);

    // And an initial message indicating that no custom attributes are present
    REQUIRE(
        CountMessages<RumGlobalAttributesChangedMessage>(test.feature_messages) == 1
    );
    REQUIRE(
        FindLastMessage<RumGlobalAttributesChangedMessage>(test.feature_messages)
            ->attributes.GetObjectPropertyCount() == 0
    );

    // When we call AddAttribute, then RemoveAttribute
    rum->AddAttribute("key", Attribute::String("value"));
    rum->RemoveAttribute("key");

    // Then we get two more RumGlobalAttributeChangedMessages, the last of which
    // reflects that our value is no longer present
    REQUIRE(
        CountMessages<RumGlobalAttributesChangedMessage>(test.feature_messages) == 3
    );
    const auto* msg =
        FindLastMessage<RumGlobalAttributesChangedMessage>(test.feature_messages);
    REQUIRE(msg->attributes.GetObjectPropertyCount() == 0);
  }

  SECTION("M emit RumGlobalAttributesChangedMessage W attributes exist on SDK start") {
    // Given a Rum feature that is not yet started
    auto rum = std::make_shared<impl::Rum>(RUM_CONFIG, clock);
    FeatureTest test(MOCK_CONTEXT);

    // And a set of custom attributes that are initialized prior to SDK start, when
    // messages are not yet being published
    rum->AddAttribute("foo", Attribute::String("hello"));
    rum->AddAttribute("bar", Attribute::Int(100));
    REQUIRE(
        CountMessages<RumGlobalAttributesChangedMessage>(test.feature_messages) == 0
    );

    // When the SDK starts
    test.Start(rum);

    // Then we get a single RumGlobalAttributeChangedMessages that describes our initial
    // set of custom attribute values, even though no AddAttribute/RemoveAttribute calls
    // have ocurred after SDK start
    REQUIRE(
        CountMessages<RumGlobalAttributesChangedMessage>(test.feature_messages) == 1
    );
    const auto* msg =
        FindLastMessage<RumGlobalAttributesChangedMessage>(test.feature_messages);
    REQUIRE(msg->attributes.GetObjectPropertyCount() == 2);
    REQUIRE(msg->attributes.GetObjectProperty("foo").GetStringValue() == "hello");
    REQUIRE(msg->attributes.GetObjectProperty("bar").GetIntValue() == 100);
  }
}
