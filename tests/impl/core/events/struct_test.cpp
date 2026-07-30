// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/events/struct.hpp"

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
    MyPropertiesWithExtra,
    extra,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_RESERVED_FIELD(future)
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

    SECTION("M omit attribute values during merge W property name is reserved") {
      // When we serialize the value with {"future":100,"y":200} as extra properties
      ev.extra.InitObject(2);
      ev.extra.SetObjectProperty("future", Attribute::Int(100));
      ev.extra.SetObjectProperty("y", Attribute::Int(200));

      // Then "y" is concatenated to the original value, but "future" is ignored because
      // it conflicts with a field name that's declared with DATADOG_JSON_RESERVED_FIELD
      RequireJsonLiteral(ev, R"({"id":"foo","name":"bar","y":200})");
    }

    SECTION("{TryEncodeJson} {DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES}") {
      SECTION("M succeed with base-only output W no extra attributes and buffer fits") {
        // Given: no extra attributes
        // When: TryEncodeJson with a buffer exactly as large as the base output
        const std::string want = R"({"id":"foo","name":"bar"})";
        std::vector<char> buf(want.size(), '\0');
        const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

        // Then: returns byte count, output matches EncodeJson
        REQUIRE(result.has_value());
        REQUIRE(*result == want.size());
        REQUIRE(std::string_view(buf.data(), buf.size()) == want);
      }

      SECTION("M return false W no extra attributes and buffer is too small") {
        // Given: no extra attributes, buffer one byte smaller than the base output
        const std::string want = R"({"id":"foo","name":"bar"})";
        REQUIRE(want.size() > 0);
        std::vector<char> buf(want.size() - 1, 'X');
        const std::vector<char> original = buf;
        const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

        // Then: returns nullopt, buffer unchanged
        REQUIRE_FALSE(result.has_value());
        REQUIRE(buf == original);
      }

      SECTION("M succeed with all extras W all extra attributes fit in buffer") {
        // Given: two extra attributes, buffer sized to fit the full output
        ev.extra.InitObject(2);
        ev.extra.SetObjectProperty("x", Attribute::Int(100));
        ev.extra.SetObjectProperty("y", Attribute::Int(200));
        const std::string want = R"({"id":"foo","name":"bar","x":100,"y":200})";
        std::vector<char> buf(want.size(), '\0');
        const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

        // Then: returns byte count, output includes both extras
        REQUIRE(result.has_value());
        REQUIRE(*result == want.size());
        REQUIRE(std::string_view(buf.data(), buf.size()) == want);
      }

      SECTION("M drop last extra W only first extra fits in buffer") {
        // Given: two extra attributes of different sizes. "x" encodes to 6 bytes
        // (,"x":1) and fits alongside the base in a 31-byte buffer; "y" encodes to
        // 34 bytes (,"y":"this-is-a-long-string-value") and does not fit there.
        ev.extra.InitObject(2);
        ev.extra.SetObjectProperty("x", Attribute::Int(1));
        ev.extra.SetObjectProperty(
            "y", Attribute::String("this-is-a-long-string-value")
        );
        const std::string want = R"({"id":"foo","name":"bar","x":1})";
        std::vector<char> buf(want.size(), '\0');
        const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

        // Then: returns byte count with only the first (smaller) extra included
        REQUIRE(result.has_value());
        REQUIRE(*result == want.size());
        REQUIRE(std::string_view(buf.data(), buf.size()) == want);
      }

      SECTION("M succeed with base-only output W all extras overflow") {
        // Given: two extra attributes. The buffer is sized for the base output only,
        // so neither extra can fit.
        ev.extra.InitObject(2);
        ev.extra.SetObjectProperty("x", Attribute::Int(100));
        ev.extra.SetObjectProperty("y", Attribute::Int(200));
        const std::string want = R"({"id":"foo","name":"bar"})";
        std::vector<char> buf(want.size(), '\0');
        const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

        // Then: returns byte count with base-only output (all extras dropped)
        REQUIRE(result.has_value());
        REQUIRE(*result == want.size());
        REQUIRE(std::string_view(buf.data(), buf.size()) == want);
      }

      SECTION("M return false W base doesn't fit even without extras") {
        // Given: extra attributes present but buffer smaller than the base output
        ev.extra.InitObject(1);
        ev.extra.SetObjectProperty("x", Attribute::Int(1));
        const std::string base = R"({"id":"foo","name":"bar"})";
        std::vector<char> buf(base.size() - 1, 'X');
        const std::vector<char> original = buf;
        const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

        // Then: returns nullopt, buffer unchanged
        REQUIRE_FALSE(result.has_value());
        REQUIRE(buf == original);
      }

      SECTION(
          "M succeed with base-only output W all extra properties have reserved names"
      ) {
        // Given: extra attributes whose names all conflict with struct fields or
        // reserved field names, so all are filtered by is_safe_name
        ev.extra.InitObject(2);
        ev.extra.SetObjectProperty("id", Attribute::Int(999));
        ev.extra.SetObjectProperty("future", Attribute::Int(999));
        const std::string want = R"({"id":"foo","name":"bar"})";
        std::vector<char> buf(want.size(), '\0');
        const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

        // Then: returns byte count, output is base-only (all extras filtered out)
        REQUIRE(result.has_value());
        REQUIRE(*result == want.size());
        REQUIRE(std::string_view(buf.data(), buf.size()) == want);
      }

      SECTION("M include safe extra W unsafe extra precedes it in index order") {
        // Given: extra[0]="id" (blocked by is_safe_name) and extra[1]="z" (safe).
        // This exercises the stateful-counter lambda: with k=2, the lambda is called
        // with idx=0 ("id", filtered) then idx=1 ("z", included). If the counter were
        // coupled incorrectly to the safe-name check — e.g. only incrementing on
        // allowed properties — index 1 would be excluded when k drops, producing wrong
        // output or including fewer properties than the buffer permits.
        ev.extra.InitObject(2);
        ev.extra.SetObjectProperty("id", Attribute::Int(999));
        ev.extra.SetObjectProperty("z", Attribute::Int(42));
        const std::string want = R"({"id":"foo","name":"bar","z":42})";
        std::vector<char> buf(want.size(), '\0');
        const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

        // Then: returns byte count, only the safe extra ("z") is included
        REQUIRE(result.has_value());
        REQUIRE(*result == want.size());
        REQUIRE(std::string_view(buf.data(), buf.size()) == want);
      }

      SECTION("M succeed with base-only output W extra is a non-object type") {
        // Given: extra attribute is an array (non-object), so GetObjectPropertyCount
        // returns 0
        ev.extra.InitArray(2);
        ev.extra.ArrayPush(Attribute::Int(1));
        ev.extra.ArrayPush(Attribute::Int(2));
        const std::string want = R"({"id":"foo","name":"bar"})";
        std::vector<char> buf(want.size(), '\0');
        const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

        // Then: returns byte count, output is base-only
        REQUIRE(result.has_value());
        REQUIRE(*result == want.size());
        REQUIRE(std::string_view(buf.data(), buf.size()) == want);
      }

      SECTION(
          "M drop extra W key escaping makes it not fit, include W buffer is exact size"
      ) {
        // The extra property key `k"ey` contains a double-quote, so the JSON-encoded
        // key is `"k\"ey"` (7 bytes) rather than the 4 raw bytes of the name.
        //
        // Full output: {"id":"foo","name":"bar","k\"ey":1} = 35 bytes
        // Extra contribution (correct): , + "k\"ey" + : + 1 = 1+7+1+1 = 10 bytes
        //
        // At a 34-byte buffer (one byte short of the full output): the bug computes
        // the extra contribution as 1+2+4+1+1 = 9 (raw name length), concludes
        // 25+9=34 fits, and then writes 35 bytes — overflowing the buffer. The fix
        // uses GetJsonSize(prop_name) = 7 for the encoded key, computes 25+10=35>34,
        // and correctly drops the attribute, writing only the 25-byte base struct.
        ev.extra.InitObject(1);
        ev.extra.SetObjectProperty("k\"ey", Attribute::Int(1));

        const std::string base = R"({"id":"foo","name":"bar"})";
        const std::string full = R"({"id":"foo","name":"bar","k\"ey":1})";
        REQUIRE(full.size() == 35);
        REQUIRE(base.size() == 25);

        // At full.size() - 1 bytes: attribute must be dropped
        {
          std::vector<char> buf(full.size() - 1, '\0');
          const auto result = TryEncodeJson(buf.data(), buf.size(), ev);
          REQUIRE(result.has_value());
          REQUIRE(*result == base.size());
          REQUIRE(std::string_view(buf.data(), *result) == base);
        }

        // At full.size() bytes: attribute must be included
        {
          std::vector<char> buf(full.size(), '\0');
          const auto result = TryEncodeJson(buf.data(), buf.size(), ev);
          REQUIRE(result.has_value());
          REQUIRE(*result == full.size());
          REQUIRE(std::string_view(buf.data(), buf.size()) == full);
        }
      }

      SECTION(
          "M drop extra W value escaping makes it not fit, include W buffer is exact "
          "size"
      ) {
        // The extra property value `a"b` contains a double-quote, so the JSON-encoded
        // value is `"a\"b"` (6 bytes) rather than the 3 raw bytes. Value size is
        // already computed via GetJsonSize(value), which handles escaping correctly;
        // this test confirms that the value path is and remains correct.
        //
        // Full output: {"id":"foo","name":"bar","z":"a\"b"} = 36 bytes
        // Extra contribution: , + "z" + : + "a\"b" = 1+3+1+6 = 11 bytes
        ev.extra.InitObject(1);
        ev.extra.SetObjectProperty("z", Attribute::String("a\"b"));

        const std::string base = R"({"id":"foo","name":"bar"})";
        const std::string full = R"({"id":"foo","name":"bar","z":"a\"b"})";
        REQUIRE(full.size() == 36);
        REQUIRE(base.size() == 25);

        // At full.size() - 1 bytes: attribute must be dropped
        {
          std::vector<char> buf(full.size() - 1, '\0');
          const auto result = TryEncodeJson(buf.data(), buf.size(), ev);
          REQUIRE(result.has_value());
          REQUIRE(*result == base.size());
          REQUIRE(std::string_view(buf.data(), *result) == base);
        }

        // At full.size() bytes: attribute must be included
        {
          std::vector<char> buf(full.size(), '\0');
          const auto result = TryEncodeJson(buf.data(), buf.size(), ev);
          REQUIRE(result.has_value());
          REQUIRE(*result == full.size());
          REQUIRE(std::string_view(buf.data(), buf.size()) == full);
        }
      }
    }
  }
}
