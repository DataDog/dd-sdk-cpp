// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/rum/command.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace datadog;
using namespace datadog::impl;

static RumCommandParams Base() { return RumCommandParams({}, {}, {}); }

TEST_CASE("RumCommand::Is", "[unit][rum]") {
  // Is<> discriminates the command's type based on its std::variant alternative
  const RumCommand cmd = RumCommand::StopView(Base(), "foo");
  REQUIRE(cmd.Is<RumStopViewPayload>() == true);
  REQUIRE(cmd.Is<RumStartActionPayload>() == false);
}

TEST_CASE("RumCommand::As", "[unit][rum]") {
  // As<> accesses the payload coerced to the given type (which must be known)
  const RumCommand cmd = RumCommand::StopView(Base(), "foo");
  const RumStopViewPayload& payload = cmd.As<RumStopViewPayload>();
  REQUIRE(payload.key == "foo");
}

TEST_CASE("RumCommand::HasFlag", "[unit][rum]") {
  // Spot-check a few flag types to verify that HasFlag resolves type traits and does
  // bitwise comparison correctly
  {
    const RumCommand sdk_init = RumCommand::SDKInit(Base());
    REQUIRE(!sdk_init.HasFlag(RumCommandFlags::UserInteraction));
    REQUIRE(!sdk_init.HasFlag(RumCommandFlags::RequiresActiveSession));
    REQUIRE(!sdk_init.HasFlag(RumCommandFlags::RequiresActiveView));
  }
  {
    const RumCommand start_view = RumCommand::StartView(Base(), "foo", "Foo");
    REQUIRE(start_view.HasFlag(RumCommandFlags::UserInteraction));
    REQUIRE(start_view.HasFlag(RumCommandFlags::RequiresActiveSession));
    REQUIRE(!start_view.HasFlag(RumCommandFlags::RequiresActiveView));
  }
  {
    const RumCommand stop_view = RumCommand::StopView(Base(), "foo");
    REQUIRE(!stop_view.HasFlag(RumCommandFlags::UserInteraction));
    REQUIRE(!stop_view.HasFlag(RumCommandFlags::RequiresActiveSession));
    REQUIRE(!stop_view.HasFlag(RumCommandFlags::RequiresActiveView));
  }
  {
    const RumCommand start_resource = RumCommand::StartResource(
        Base(), "foo", RumRequestDetails{RumResourceMethod::Get, "/foo"}
    );
    REQUIRE(!start_resource.HasFlag(RumCommandFlags::UserInteraction));
    REQUIRE(start_resource.HasFlag(RumCommandFlags::RequiresActiveSession));
    REQUIRE(start_resource.HasFlag(RumCommandFlags::RequiresActiveView));
  }
  {
    const RumCommand stop_resource = RumCommand::StopResource(Base(), "foo");
    REQUIRE(!stop_resource.HasFlag(RumCommandFlags::UserInteraction));
    REQUIRE(!stop_resource.HasFlag(RumCommandFlags::RequiresActiveSession));
    REQUIRE(!stop_resource.HasFlag(RumCommandFlags::RequiresActiveView));
  }
  {
    const RumCommand start_action =
        RumCommand::StartAction(Base(), RumActionType::Tap, "foo");
    REQUIRE(start_action.HasFlag(RumCommandFlags::UserInteraction));
    REQUIRE(start_action.HasFlag(RumCommandFlags::RequiresActiveSession));
    REQUIRE(start_action.HasFlag(RumCommandFlags::RequiresActiveView));
  }
  {
    const RumCommand stop_action = RumCommand::StopAction(Base(), "foo");
    REQUIRE(stop_action.HasFlag(RumCommandFlags::UserInteraction));
    REQUIRE(stop_action.HasFlag(RumCommandFlags::RequiresActiveSession));
    REQUIRE(!stop_action.HasFlag(RumCommandFlags::RequiresActiveView));
  }
}
