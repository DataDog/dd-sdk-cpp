// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/rum/context.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("RumContext::ToFeatureContext", "[unit][rum]") {
  SECTION("M set no values W application_id invalid") {
    // Given a RUM state snapshot that has some UUIDs but no valid application ID
    impl::RumContext snapshot;
    snapshot.application_id = UUID::Zero;
    snapshot.session_id = UUID::Random();

    // When we populate a RumFeatureContext from that snapshot
    const RumFeatureContext ctx = snapshot.ToFeatureContext();

    // Then all values are zero: there is no valid RUM context to inject
    REQUIRE(ctx.application_id == UUID::Zero);
    REQUIRE(ctx.session_id == UUID::Zero);
    REQUIRE(ctx.view_id == UUID::Zero);
    REQUIRE(ctx.action_id == UUID::Zero);
    REQUIRE(ctx.view_name.empty());
  }

  SECTION("M set application_id W application_id is valid") {
    // Given a RUM state snapshot that has a valid application_id and no other state
    impl::RumContext snapshot;
    snapshot.application_id = *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf");
    snapshot.session_id = UUID::Zero;

    // When we populate a RumFeatureContext from that snapshot
    const RumFeatureContext ctx = snapshot.ToFeatureContext();

    // Then we have a valid application_id and nothing else
    REQUIRE(ctx.application_id == *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf"));
    REQUIRE(ctx.session_id == UUID::Zero);
    REQUIRE(ctx.view_id == UUID::Zero);
    REQUIRE(ctx.action_id == UUID::Zero);
    REQUIRE(ctx.view_name.empty());
  }

  SECTION("M set session_id W session has valid ID and is active and sampled") {
    // Given a RUM state snapshot that has a valid application_id and an active, sampled
    // session, but no active view
    impl::RumContext snapshot;
    snapshot.application_id = *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf");
    snapshot.session_id = *UUID::Parse("ea3a7d2e-1862-4731-bdf1-ac3162811ba8");
    snapshot.session_is_active = true;
    snapshot.session_is_sampled = true;

    // When we populate a RumFeatureContext from that snapshot
    const RumFeatureContext ctx = snapshot.ToFeatureContext();

    // Then we have a valid application_id and session_id
    REQUIRE(ctx.application_id == *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf"));
    REQUIRE(ctx.session_id == *UUID::Parse("ea3a7d2e-1862-4731-bdf1-ac3162811ba8"));
    REQUIRE(ctx.view_id == UUID::Zero);
    REQUIRE(ctx.action_id == UUID::Zero);
    REQUIRE(ctx.view_name.empty());
  }

  SECTION("M set view_id W active session has an active view") {
    // Given a RUM state snapshot that has a valid application_id and an active, sampled
    // session with an active view
    impl::RumContext snapshot;
    snapshot.application_id = *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf");
    snapshot.session_id = *UUID::Parse("ea3a7d2e-1862-4731-bdf1-ac3162811ba8");
    snapshot.session_is_active = true;
    snapshot.session_is_sampled = true;
    snapshot.active_view_id = *UUID::Parse("76cb171c-c6d5-4719-adfc-f90c6b57c0a0");
    snapshot.active_view_key = "foo";
    snapshot.active_view_name = "Foo";

    // When we populate a RumFeatureContext from that snapshot
    const RumFeatureContext ctx = snapshot.ToFeatureContext();

    // Then we have a valid application_id, session_id, and view_id
    REQUIRE(ctx.application_id == *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf"));
    REQUIRE(ctx.session_id == *UUID::Parse("ea3a7d2e-1862-4731-bdf1-ac3162811ba8"));
    REQUIRE(ctx.view_id == *UUID::Parse("76cb171c-c6d5-4719-adfc-f90c6b57c0a0"));
    REQUIRE(ctx.action_id == UUID::Zero);
    REQUIRE(ctx.view_name == "Foo");
  }

  SECTION("M set action_id W active view has an active action") {
    // Given a RUM state snapshot that has a valid application_id, an active, sampled
    // session with an active view, and an active action within that view
    impl::RumContext snapshot;
    snapshot.application_id = *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf");
    snapshot.session_id = *UUID::Parse("ea3a7d2e-1862-4731-bdf1-ac3162811ba8");
    snapshot.session_is_active = true;
    snapshot.session_is_sampled = true;
    snapshot.active_view_id = *UUID::Parse("76cb171c-c6d5-4719-adfc-f90c6b57c0a0");
    snapshot.active_view_key = "foo";
    snapshot.active_view_name = "Foo";
    snapshot.active_action_id = *UUID::Parse("860a7264-f20a-4833-8e7c-10f15ef6f873");

    // When we populate a RumFeatureContext from that snapshot
    const RumFeatureContext ctx = snapshot.ToFeatureContext();

    // Then we have a valid application_id, session_id, view_id, and action_id
    REQUIRE(ctx.application_id == *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf"));
    REQUIRE(ctx.session_id == *UUID::Parse("ea3a7d2e-1862-4731-bdf1-ac3162811ba8"));
    REQUIRE(ctx.view_id == *UUID::Parse("76cb171c-c6d5-4719-adfc-f90c6b57c0a0"));
    REQUIRE(ctx.action_id == *UUID::Parse("860a7264-f20a-4833-8e7c-10f15ef6f873"));
    REQUIRE(ctx.view_name == "Foo");
  }

  SECTION("M not set session_id, view_id, or action_id W session is not sampled") {
    // Given a RUM state snapshot that has a valid application_id and an active session
    // that's not sampled
    impl::RumContext snapshot;
    snapshot.application_id = *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf");
    snapshot.session_id = *UUID::Parse("ea3a7d2e-1862-4731-bdf1-ac3162811ba8");
    snapshot.session_is_active = true;
    snapshot.session_is_sampled = false;

    // When we populate a RumFeatureContext from that snapshot
    const RumFeatureContext ctx = snapshot.ToFeatureContext();

    // Then we have a valid application_id, but no session_id: RUM won't be sending data
    // to the backend for the session in question, so we shouldn't tell other features
    // about it
    REQUIRE(ctx.application_id == *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf"));
    REQUIRE(ctx.session_id == UUID::Zero);
    REQUIRE(ctx.view_id == UUID::Zero);
    REQUIRE(ctx.action_id == UUID::Zero);
    REQUIRE(ctx.view_name.empty());
  }

  SECTION("M not set session_id, view_id, or action_id W session is no longer active") {
    // Given a RUM state snapshot that has a valid application_id and a session that's
    // sampled but is no longer active
    impl::RumContext snapshot;
    snapshot.application_id = *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf");
    snapshot.session_id = *UUID::Parse("ea3a7d2e-1862-4731-bdf1-ac3162811ba8");
    snapshot.session_is_active = false;
    snapshot.session_is_sampled = true;

    // ...including details of the last active view in our no-longer-active session
    snapshot.active_view_id = *UUID::Parse("76cb171c-c6d5-4719-adfc-f90c6b57c0a0");
    snapshot.active_view_key = "foo";
    snapshot.active_view_name = "Foo";

    // When we populate a RumFeatureContext from that snapshot
    const RumFeatureContext ctx = snapshot.ToFeatureContext();

    // Then we have a valid application_id, but no session_id: the session is no longer
    // active, so events generated outside of RUM don't need to be correlated with it
    REQUIRE(ctx.application_id == *UUID::Parse("2138a97c-af75-4972-be50-8448db997abf"));
    REQUIRE(ctx.session_id == UUID::Zero);
    REQUIRE(ctx.view_id == UUID::Zero);
    REQUIRE(ctx.action_id == UUID::Zero);
    REQUIRE(ctx.view_name.empty());
  }
}
