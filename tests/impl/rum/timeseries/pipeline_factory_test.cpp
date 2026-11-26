// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/pipeline_factory.hpp"

#include <catch2/catch_test_macros.hpp>

#include "impl/rum/timeseries/fakes.hpp"
#include "mock/clock.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;
using namespace timeseries_test;

namespace {
PipelineFactory MakeFactory(
    TimeseriesConfig config, DiagnosticMessageBuffer& diagnostics
) {
  static MockClock clock;
  return PipelineFactory(
      std::move(config),
      clock,
      std::make_shared<CollectedTimeseriesSessionRegistry>(),
      [](ContextThreadFunc) {},
      diagnostics.CreateTestLogger()
  );
}
}  // namespace

TEST_CASE("PipelineFactory", "[unit][rum][timeseries]") {
  SECTION("M skip and log each type W its pipeline cannot be created") {
    DiagnosticMessageBuffer diagnostics;

    SECTION(
        "M log an error per type W default config (no CPU or memory pipeline yet)"
    ) {
      const PipelineFactory factory = MakeFactory(TimeseriesConfig{}, diagnostics);
      CHECK(factory.Create(BoundSession()).empty());
      REQUIRE(diagnostics.error.size() == 2);
      CHECK(diagnostics.error[0] == R"(Pipeline creation failed {"type":"cpu"})");
      CHECK(diagnostics.error[1] == R"(Pipeline creation failed {"type":"memory"})");
    }

    SECTION("M log nothing W no types configured") {
      TimeseriesConfig config;
      config.types.clear();
      const PipelineFactory factory = MakeFactory(config, diagnostics);
      CHECK(factory.Create(BoundSession()).empty());
      CHECK(diagnostics.TotalSize() == 0);
    }
  }

  SECTION("M create nothing and warn W session ID is zero") {
    DiagnosticMessageBuffer diagnostics;
    const PipelineFactory factory = MakeFactory(TimeseriesConfig{}, diagnostics);

    const TimeseriesBoundSession zero{
        SomeApplication(), UUID::Zero, RumSessionType::User
    };
    CHECK(factory.Create(zero).empty());
    CHECK(diagnostics.warning.size() == 1);
    CHECK(diagnostics.TotalSize() == 1);  // no per-type error: nothing was attempted
  }
}
