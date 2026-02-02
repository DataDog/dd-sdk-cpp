// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/events/struct.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <optional>
#include <string>

#include "datadog/attribute.hpp"
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

struct MyPropertiesWithExtra {
  std::string id;
  std::string name;
  Attribute extra;
};

DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    MyPropertiesWithExtra, extra, DATADOG_JSON_FIELD(id), DATADOG_JSON_FIELD(name)
)

TEST_CASE("struct JSON serialization", "[unit][events]") {
  const UUID uuid_6ade = *UUID::Parse("6ade2e39-c3c4-42a9-93f5-d004e2cc452f");
  const UUID uuid_e294 = *UUID::Parse("e29459c7-2a4a-47da-93d9-2fd197c8a8f6");
  const Timestamp tp_13h26m{std::chrono::microseconds(1761312404104719)};
  const Timestamp tp_13h28m{std::chrono::microseconds(1761312501788174)};

  SECTION("M render DATADOG_JSON_STRUCT-annotated struct as JSON object") {
    MyEvent ev{"foo", uuid_6ade, tp_13h26m, std::nullopt};
    RequireJsonLiteral(
        ev,
        R"({"type":"foo","id":"6ade2e39-c3c4-42a9-93f5-d004e2cc452f","timestamp":"2025-10-24T13:26:44.104Z","tags":null})"
    );
  }

  SECTION("M render nested struct values as nested JSON objects") {
    MyEvent ev_1{"foo", uuid_6ade, tp_13h26m, std::nullopt};
    MyEvent ev_2{"bar", uuid_e294, tp_13h28m, "good;spicy;qu\"ote"};
    MyCompoundType compound{ev_2, ev_1};
    RequireJsonLiteral(
        compound,
        R"({"event":{"type":"bar","id":"e29459c7-2a4a-47da-93d9-2fd197c8a8f6","timestamp":"2025-10-24T13:28:21.788Z","tags":"good;spicy;qu\"ote"},"prev":{"type":"foo","id":"6ade2e39-c3c4-42a9-93f5-d004e2cc452f","timestamp":"2025-10-24T13:26:44.104Z","tags":null}})"
    );
  }

  SECTION("{DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES}") {
    // Given a struct value that should serialize to {"id":"foo","name":"bar"}, but
    // whose 'extra' member permits an arbitrary set of user-specified attributes to be
    // merged into that object
    MyPropertiesWithExtra ev{"foo", "bar", {}};

    SECTION("M render struct as JSON object W no custom attributes are present") {
      // When we serialize the value with extra at the default of Attribute::Null()
      // Then the resulting value has both "id" and "name" values, and nothing else
      RequireJsonLiteral(ev, R"({"id":"foo","name":"bar"})");
    }

    SECTION("M render struct as JSON object W custom attributes have non-object type") {
      // When we explicitly set extra to a non-object value
      ev.extra.InitArray(2);
      ev.extra.ArrayPush(Attribute::Int(100));
      ev.extra.ArrayPush(Attribute::Int(200));

      // Then the resulting value has both "id" and "name" values, and nothing else
      RequireJsonLiteral(ev, R"({"id":"foo","name":"bar"})");
    }

    SECTION(
        "M merge attribute values into top-level object W custom attributes are present"
    ) {
      // When we serialize the value with {"x":100,"y":200} as extra properties
      ev.extra.InitObject(2);
      ev.extra.SetObjectProperty("x", Attribute::Int(100));
      ev.extra.SetObjectProperty("y", Attribute::Int(200));

      // Then x and y are concatenated to the original value
      RequireJsonLiteral(ev, R"({"id":"foo","name":"bar","x":100,"y":200})");
    }

    SECTION("M omit attribute values during merge W name conflicts with struct field") {
      // When we serialize the value with {"id":100,"y":200} as extra properties
      ev.extra.InitObject(2);
      ev.extra.SetObjectProperty("id", Attribute::Int(100));
      ev.extra.SetObjectProperty("y", Attribute::Int(200));

      // Then y is concatenated to the original value, but id is ignored so as not to
      // conflict with the canonical 'id' member on the struct itself
      RequireJsonLiteral(ev, R"({"id":"foo","name":"bar","y":200})");
    }

    SECTION("M omit attribute values during merge W name conflicts with struct field") {
      // When we serialize the value with {"id":100,"name":200} as extra properties
      ev.extra.InitObject(2);
      ev.extra.SetObjectProperty("id", Attribute::Int(100));
      ev.extra.SetObjectProperty("name", Attribute::Int(200));

      // Then the resulting value has both "id" and "name" values, and nothing else: our
      // extra attributes are entirely ignored, and nothing is concatenated
      RequireJsonLiteral(ev, R"({"id":"foo","name":"bar"})");
    }
  }
}
