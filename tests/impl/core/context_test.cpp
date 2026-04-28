// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/version.hpp"

using namespace datadog::impl;
using namespace datadog;

TEST_CASE("CoreContext", "[unit]") {
  // Given valid OS info and device info
  platform::OsInfo os_info{"TestOS", "1.2.3", "12345", "1"};
  platform::DeviceInfo device_info{
      "desktop",
      "test-device",
      "test-model",
      "test-brand",
      "x86_64",
      "en-US",
      "America/New_York"
  };

  // When CoreContext is constructed with OS info and device info
  CoreConfig config("token", "service", "env");
  config.SetApplicationVersion("3.0.0");
  ImmutableContext imm(config, os_info, device_info, "reporter", "1.2.3");
  CoreContext ctx(imm);

  // Then OS info is accessible and matches
  REQUIRE(ctx.os != nullptr);
  REQUIRE(ctx.os->name == "TestOS");
  REQUIRE(ctx.os->version == "1.2.3");
  REQUIRE(ctx.os->build == "12345");
  REQUIRE(ctx.os->version_major == "1");

  // And device info is accessible and matches
  REQUIRE(ctx.device != nullptr);
  REQUIRE(ctx.device->type == "desktop");
  REQUIRE(ctx.device->name == "test-device");
  REQUIRE(ctx.device->model == "test-model");
  REQUIRE(ctx.device->brand == "test-brand");
  REQUIRE(ctx.device->architecture == "x86_64");
  REQUIRE(ctx.device->locale == "en-US");
  REQUIRE(ctx.device->time_zone == "America/New_York");

  // And basic SDK configuration detail are accessible and formatted as expected
  REQUIRE(ctx.client_token == "token");
  REQUIRE(ctx.service == "service");
  REQUIRE(ctx.env == "env");
  REQUIRE(ctx.application_version == "3.0.0");
  REQUIRE(ctx.source == "rum-cpp");
  REQUIRE(ctx.sdk_version == SDK_VERSION);
  REQUIRE(ctx.intake_origin == "https://browser-intake-datadoghq.com");
  REQUIRE(ctx.user_agent == "service/3.0.0 reporter/1.2.3 (test-device; TestOS/1.2.3)");

  // And feature-specific context values are initially nil
  REQUIRE(!ctx.rum.has_value());

  SECTION("CoreContextProvider") {
    // Given a CoreContextProvider that's initialized from our initial CoreContext value
    CoreContextProvider provider(ctx);

    // When we call Get() to retrieve a snapshot of our CoreContext
    const CoreContext snapshot_a = provider.Get();

    // And call Update() to mutate the RUM-feature-specific context, then get another
    // snapshot
    provider.Update([](CoreContext& mut_ctx) {
      mut_ctx.rum.emplace();
      mut_ctx.rum->application_id =
          *UUID::Parse("982b1704-9764-4912-9f82-0f6063383ad9");
    });
    const CoreContext snapshot_b = provider.Get();

    // Then our original snapshot still reflects the initial state, while the new
    // snapshot reflects the changes we made
    REQUIRE(!snapshot_a.rum.has_value());
    REQUIRE(snapshot_b.rum.has_value());
    REQUIRE(
        snapshot_b.rum->application_id ==
        *UUID::Parse("982b1704-9764-4912-9f82-0f6063383ad9")
    );

    // And all immutable values are preserved identically in both snapshots
    REQUIRE(snapshot_a.os == snapshot_b.os);
    REQUIRE(snapshot_a.device == snapshot_b.device);
    REQUIRE(snapshot_a.client_token.data() == snapshot_b.client_token.data());
    REQUIRE(snapshot_b.client_token == "token");
    REQUIRE(snapshot_b.service == "service");
    REQUIRE(snapshot_b.env == "env");
    REQUIRE(snapshot_b.application_version == "3.0.0");
    REQUIRE(snapshot_b.source == "rum-cpp");
    REQUIRE(snapshot_b.sdk_version == SDK_VERSION);
    REQUIRE(snapshot_b.intake_origin == "https://browser-intake-datadoghq.com");
    REQUIRE(
        snapshot_b.user_agent ==
        "service/3.0.0 reporter/1.2.3 (test-device; TestOS/1.2.3)"
    );
    REQUIRE(snapshot_a.user_agent.data() == snapshot_b.user_agent.data());
  }
}
