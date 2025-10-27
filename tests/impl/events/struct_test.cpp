// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "events/struct.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <string>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

struct MyEvent {
  std::string type;
  UUID id;
  Timestamp timestamp;
  std::optional<std::string> tags;
};

DATADOG_JSON_STRUCT(
    MyEvent,
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(timestamp),
    DATADOG_JSON_FIELD(tags)
)

struct MyCompoundType {
  MyEvent event;
  std::optional<MyEvent> prev_event;
};

DATADOG_JSON_STRUCT(
    MyCompoundType,
    DATADOG_JSON_FIELD(event),
    DATADOG_JSON_FIELD_NAME(prev_event, "prev")
)

TEST_CASE("struct JSON serialization", "[unit][events]") {
  const UUID uuid_6ade = *UUID::Parse("6ade2e39-c3c4-42a9-93f5-d004e2cc452f");
  const UUID uuid_e294 = *UUID::Parse("e29459c7-2a4a-47da-93d9-2fd197c8a8f6");
  const Timestamp tp_13h26m{std::chrono::microseconds(1761312404104719)};
  const Timestamp tp_13h28m{std::chrono::microseconds(1761312501788174)};

  SECTION("M render DATADOG_JSON_STRUCT-annotated struct as JSON object") {
    MyEvent ev{"foo", uuid_6ade, tp_13h26m, std::nullopt};
    RequireJsonValue(
        ev,
        R"({"type":"foo","id":"6ade2e39-c3c4-42a9-93f5-d004e2cc452f","timestamp":"2025-10-24T13:26:44.104Z","tags":null})"
    );
  }

  SECTION("M render nested struct values as nested JSON objects") {
    MyEvent ev_1{"foo", uuid_6ade, tp_13h26m, std::nullopt};
    MyEvent ev_2{"bar", uuid_e294, tp_13h28m, "good;spicy;qu\"ote"};
    MyCompoundType compound{ev_2, ev_1};
    RequireJsonValue(
        compound,
        R"({"event":{"type":"bar","id":"e29459c7-2a4a-47da-93d9-2fd197c8a8f6","timestamp":"2025-10-24T13:28:21.788Z","tags":"good;spicy;qu\"ote"},"prev":{"type":"foo","id":"6ade2e39-c3c4-42a9-93f5-d004e2cc452f","timestamp":"2025-10-24T13:26:44.104Z","tags":null}})"
    );
  }
}
