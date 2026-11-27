// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/pipeline_factory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/platform/cpu_info_provider.hpp"

#include "impl/rum/timeseries/fakes.hpp"
#include "mock/clock.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;
using namespace timeseries_test;

namespace {

/** Reports one more hour of CPU time on every read: every value clamps to 100 %. */
class SaturatedCpu final : public platform::ICpuInfoProvider {
 public:
  std::optional<Duration> CpuTime() const override {
    return _cpu_time += std::chrono::hours(1);
  }
  std::optional<uint32_t> LogicalCpuCount() const override { return 1; }

 private:
  mutable Duration _cpu_time{};
};

PipelineFactory MakeFactory(
    TimeseriesConfig config,
    DiagnosticMessageBuffer& diagnostics,
    RunOnContextThread run_on_context_thread = [](ContextThreadFunc) {}
) {
  static MockClock clock;
  return PipelineFactory(
      std::move(config),
      clock,
      std::make_shared<SaturatedCpu>(),
      std::make_shared<CollectedTimeseriesSessionRegistry>(),
      std::move(run_on_context_thread),
      diagnostics.CreateTestLogger()
  );
}

TimeseriesConfig OnlyTypes(std::set<TimeseriesType> types) {
  TimeseriesConfig config;
  config.types = std::move(types);
  return config;
}
}  // namespace

TEST_CASE("PipelineFactory", "[unit][rum][timeseries]") {
  SECTION("M skip and log each type W its pipeline cannot be created") {
    DiagnosticMessageBuffer diagnostics;

    SECTION("M log an error for memory W default config (no memory pipeline yet)") {
      const PipelineFactory factory = MakeFactory(TimeseriesConfig{}, diagnostics);
      CHECK(factory.Create(BoundSession()).size() == 1);  // CPU only
      REQUIRE(diagnostics.error.size() == 1);
      CHECK(diagnostics.error[0] == R"(Pipeline creation failed {"type":"memory"})");
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
        SomeApplication(),
        UUID::Zero,
        RumSessionType::User,
        RumSessionPrecondition::UserAppLaunch,
        100.0f,
    };
    CHECK(factory.Create(zero).empty());
    CHECK(diagnostics.warning.size() == 1);
    CHECK(diagnostics.TotalSize() == 1);  // no per-type error: nothing was attempted
  }

  SECTION("M create a CPU pipeline W CPU is configured") {
    DiagnosticMessageBuffer diagnostics;
    TimeseriesConfig config = OnlyTypes({TimeseriesType::Cpu});
    config.sampling_interval = std::chrono::milliseconds(250);
    const PipelineFactory factory = MakeFactory(config, diagnostics);

    const std::vector<std::shared_ptr<IPipeline>> pipelines =
        factory.Create(BoundSession());
    REQUIRE(pipelines.size() == 1);
    REQUIRE(pipelines[0] != nullptr);
    CHECK(pipelines[0]->Interval() == std::chrono::milliseconds(250));
    CHECK(diagnostics.TotalSize() == 0);
  }

  SECTION("M write a CPU event for the bound session W the CPU buffer fills up") {
    DiagnosticMessageBuffer diagnostics;
    TimeseriesConfig config = OnlyTypes({TimeseriesType::Cpu});
    config.buffer_size = 2;
    std::vector<ContextThreadFunc> posted;
    const PipelineFactory factory =
        MakeFactory(config, diagnostics, [&posted](ContextThreadFunc func) {
          posted.push_back(std::move(func));
        });
    const std::vector<std::shared_ptr<IPipeline>> pipelines =
        factory.Create(BoundSession());
    REQUIRE(pipelines.size() == 1);

    // The first sample only takes the baseline, and the steady clock may need a few
    // samples to move
    for (int i = 0; i < 1000 && posted.empty(); i++) {
      pipelines[0]->Execute();
    }
    REQUIRE(posted.size() == 1);

    WriterCapture writer_capture;
    const CoreContext ctx = ContextWithSession(OtherSession());
    posted[0](ctx, writer_capture.Writer(), [](FeatureMessage) { return true; });
    REQUIRE(writer_capture.Count() == 1);
    writer_capture.CheckCleanWrites();

    const nlohmann::json event = nlohmann::json::parse(writer_capture.events[0]);
    CHECK(event["type"] == "timeseries");
    CHECK(event["timeseries"]["name"] == "cpu");
    CHECK(event["session"]["id"] == SomeSession().ToString());
    const nlohmann::json& values = event["timeseries"]["data"]["values"]["cpu_usage"];
    REQUIRE_FALSE(values.empty());
    CHECK(values.size() == event["timeseries"]["data"]["timestamps"].size());
    for (const nlohmann::json& value : values) {
      CHECK(value.get<double>() == 100.0);  // read from the injected provider
    }
    CHECK(diagnostics.error.empty());
  }
}
