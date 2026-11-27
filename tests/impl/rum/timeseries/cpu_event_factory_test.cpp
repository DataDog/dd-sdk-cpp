// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/cpu_event_factory.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "impl/rum/timeseries/fakes.hpp"
#include "support/context.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;
using namespace timeseries_test;

namespace {

Timestamp At(int64_t ns) { return Timestamp{std::chrono::nanoseconds(ns)}; }

nlohmann::json EncodeToJson(
    CpuEventFactory& factory,
    const CoreContext& ctx,
    const TimeseriesBoundSession& session,
    const std::vector<DataPoint<double>>& points
) {
  const std::optional<std::string_view> json = factory.Encode(ctx, session, points);
  REQUIRE(json.has_value());
  REQUIRE_FALSE(json->empty());
  return nlohmann::json::parse(*json);
}

}  // namespace

TEST_CASE("CpuEventFactory", "[unit][rum][timeseries]") {
  CpuEventFactory factory;
  TimeseriesBoundSession session = BoundSession();
  const std::vector<DataPoint<double>> points{
      {At(1761829844000000001), 12.5},
      {At(1761829845000000002), 0.0},
      {At(1761829846000000003), 99.75},
  };

  SECTION("M encode a timeseries cpu event W a batch is encoded") {
    const nlohmann::json got = EncodeToJson(factory, MOCK_CONTEXT, session, points);
    RequireEventMatch(got, DATADOG_RUM_EVENT_LITERAL(R"({
      "date": 1761829844000,
      "application": {
        "id": "09090909-0808-0808-0707-070706060606"
      },
      "ddtags": "service:mock-service,env:mock-env,sdk_version:1.2.3",
      "session": {
        "id": "01020304-0506-0708-090a-0b0c0d0e0f10",
        "type": "user",
        "is_main_process": true,
        "has_timeseries": true
      },
      "os": {
        "name": "mock-os",
        "version": "2.3.4",
        "build": "mock-build-number",
        "version_major": "2"
      },
      "device": {
        "type": "desktop",
        "name": "mock-device",
        "model": "mock-model",
        "brand": "mock-brand",
        "architecture": "x86_64",
        "locale": "en-US",
        "time_zone": "America/New_York"
      },
      "_dd": {
        "format_version": 2,
        "session": {
          "session_precondition": "inactivity_timeout"
        },
        "configuration": {
          "session_sample_rate": 25.0
        }
      },
      "type": "timeseries",
      "timeseries": {
        "id": "${__NONZERO_UUID__}",
        "name": "cpu",
        "schema": "object-v2",
        "start": 1761829844000000001,
        "end": 1761829846000000003,
        "data": {
          "timestamps": [
            1761829844000000001,
            1761829845000000002,
            1761829846000000003
          ],
          "values": {
            "cpu_usage": [12.5, 0.0, 99.75]
          }
        }
      }
    })"));
  }

  SECTION("M drop non-finite values with their timestamps W the batch has some") {
    const std::vector<DataPoint<double>> mixed{
        {At(1000), std::numeric_limits<double>::quiet_NaN()},
        {At(2000), 10.0},
        {At(3000), std::numeric_limits<double>::infinity()},
        {At(4000), 20.0},
        {At(5000), -std::numeric_limits<double>::infinity()},
    };
    const nlohmann::json got = EncodeToJson(factory, MOCK_CONTEXT, session, mixed);
    const nlohmann::json& timeseries = got["timeseries"];
    CHECK(timeseries["data"]["timestamps"] == nlohmann::json::array({2000, 4000}));
    CHECK(
        timeseries["data"]["values"]["cpu_usage"] == nlohmann::json::array({10.0, 20.0})
    );
    CHECK(timeseries["start"] == 2000);
    CHECK(timeseries["end"] == 4000);
    CHECK(got["date"] == 0);  // 2000 ns, in ms
  }

  SECTION("M encode nothing W no value is finite") {
    const std::vector<DataPoint<double>> invalid{
        {At(1000), std::numeric_limits<double>::quiet_NaN()},
        {At(2000), std::numeric_limits<double>::infinity()},
    };
    CHECK_FALSE(factory.Encode(MOCK_CONTEXT, session, invalid).has_value());
  }

  SECTION("M encode nothing W the batch is empty") {
    CHECK_FALSE(factory.Encode(MOCK_CONTEXT, session, {}).has_value());
  }

  SECTION("M use the bound session W ctx.rum describes another session") {
    const std::string bound_id = SomeSession().ToString();
    const std::string application_id = SomeApplication().ToString();

    SECTION("other session") {
      const nlohmann::json got =
          EncodeToJson(factory, ContextWithSession(OtherSession()), session, points);
      CHECK(got["session"]["id"] == bound_id);
      CHECK(got["application"]["id"] == application_id);
    }

    SECTION("zero session") {
      const nlohmann::json got =
          EncodeToJson(factory, ContextWithSession(UUID::Zero), session, points);
      CHECK(got["session"]["id"] == bound_id);
      CHECK(got["application"]["id"] == application_id);
    }

    SECTION("no RUM context") {
      const nlohmann::json got = EncodeToJson(factory, MOCK_CONTEXT, session, points);
      CHECK(got["session"]["id"] == bound_id);
      CHECK(got["application"]["id"] == application_id);
    }
  }

  SECTION("M use the bound session type W the session is synthetics") {
    session.type = RumSessionType::Synthetics;
    const nlohmann::json got = EncodeToJson(factory, MOCK_CONTEXT, session, points);
    CHECK(got["session"]["type"] == "synthetics");
  }

  SECTION("M generate a new timeseries ID W each batch is encoded") {
    const nlohmann::json first = EncodeToJson(factory, MOCK_CONTEXT, session, points);
    const nlohmann::json second = EncodeToJson(factory, MOCK_CONTEXT, session, points);
    const std::optional<UUID> first_id =
        UUID::Parse(first["timeseries"]["id"].get<std::string>());
    const std::optional<UUID> second_id =
        UUID::Parse(second["timeseries"]["id"].get<std::string>());
    REQUIRE(first_id.has_value());
    REQUIRE(second_id.has_value());
    CHECK(*first_id != UUID::Zero);
    CHECK(*first_id != *second_id);
  }
}
