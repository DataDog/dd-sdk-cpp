// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "json.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <string_view>
#include <vector>

#include "datadog/uuid.hpp"
#include "events/struct.hpp"

namespace datadog::impl {
struct JsonTestEvent {
  UUID id;
  uint64_t foo{0};
  std::string bar;
};
DATADOG_JSON_STRUCT(
    JsonTestEvent,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(foo),
    DATADOG_JSON_FIELD(bar)
)
};  // namespace datadog::impl

struct JsonBuffer {
  std::vector<uint8_t> bytes;
  std::string_view ToString() const {
    return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
};

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("EncodeJson", "[unit][json]") {
  // Given a statically-defined event payload and a buffer to write it to
  JsonTestEvent ev{*UUID::Parse("56c031d0-24e3-4fb3-bba5-8ac53b4041d1"), 42, "hi"};
  JsonBuffer buf;

  SECTION("M write value to buffer as JSON") {
    // When we call EncodeJson
    EncodeJson(buf.bytes, ev);

    // Then the buffer contains the details of our event, serialized as a JSON object
    REQUIRE(
        buf.ToString() ==
        R"({"id":"56c031d0-24e3-4fb3-bba5-8ac53b4041d1","foo":42,"bar":"hi"})"
    );
  }
}

TEST_CASE("EncodeJsonWithMergedUserAttributes", "[unit][json]") {
  // Given a statically-defined event payload, a buffer to write JSON values to, and an
  // object Attribute that will be used for merging multiple attribute values
  JsonTestEvent ev{*UUID::Parse("56c031d0-24e3-4fb3-bba5-8ac53b4041d1"), 42, "hi"};
  JsonBuffer buf;
  Attribute obj = Attribute::Object(8);

  SECTION("M write same value as EncodeJson W no attributes given") {
    // When we encode the event by itself, with an empty set of user attributes
    EncodeJsonWithMergedUserAttributes(buf.bytes, ev, obj, {}, nullptr);

    // Then we get the exact same result as if we had called plain ol' EncodeJson
    REQUIRE(
        buf.ToString() ==
        R"({"id":"56c031d0-24e3-4fb3-bba5-8ac53b4041d1","foo":42,"bar":"hi"})"
    );
  }

  SECTION("M merge user attributes W given a single object") {
    // Given an object with user-defined attributes
    Attribute attributes = Attribute::Object(4);
    attributes.SetObjectProperty("x", Attribute::Int(100));
    attributes.SetObjectProperty("y", Attribute::Int(200));

    // When we encode our event payload with those extra attributes included
    EncodeJsonWithMergedUserAttributes(buf.bytes, ev, obj, {attributes}, nullptr);

    // Then we get the same set of event values, with our extra attributes tacked on as
    // additional properties of the top-level JSON object
    REQUIRE(
        buf.ToString() ==
        R"({"id":"56c031d0-24e3-4fb3-bba5-8ac53b4041d1","foo":42,"bar":"hi","x":100,"y":200})"
    );
  }

  SECTION("M merge user attributes W given multiple objects") {
    // Given two objects with user-defined attributes, both with a 'y' value
    Attribute attributes_a = Attribute::Object(4);
    attributes_a.SetObjectProperty("x", Attribute::Int(100));
    attributes_a.SetObjectProperty("y", Attribute::Int(200));
    Attribute attributes_b = Attribute::Object(4);
    attributes_b.SetObjectProperty("y", Attribute::Int(300));
    attributes_b.SetObjectProperty("z", Attribute::Int(400));

    // When we encode our event payload with those extra attributes included, with
    // attributes_b appearing in the list after attributes_a
    EncodeJsonWithMergedUserAttributes(
        buf.bytes, ev, obj, {attributes_a, attributes_b}, nullptr
    );

    // Then all our extra attributes are merged in, and the 'y' value from attributes_b
    // takes precedence
    REQUIRE(
        buf.ToString() ==
        R"({"id":"56c031d0-24e3-4fb3-bba5-8ac53b4041d1","foo":42,"bar":"hi","x":100,"y":300,"z":400})"
    );
  }

  SECTION("M avoid property name conflicts W given a filter function") {
    // Given an object with user-defined attributes, some of which use property names
    // that conflict with fields of our base event type
    Attribute attributes = Attribute::Object(4);
    attributes.SetObjectProperty("x", Attribute::Int(100));
    attributes.SetObjectProperty("y", Attribute::Int(200));
    attributes.SetObjectProperty("foo", Attribute::Int(300));
    attributes.SetObjectProperty("bar", Attribute::Int(400));

    // And a function that describes which names are reserved
    auto is_acceptable_property_name = [](std::string_view name) {
      return name != "id" && name != "foo" && name != "bar";
    };

    // When we encode our event payload with extra attributes, and our filter func
    EncodeJsonWithMergedUserAttributes(
        buf.bytes, ev, obj, {attributes}, is_acceptable_property_name
    );

    // Then our acceptable attributes 'x' and 'y' are merged in, while 'foo' and 'bar'
    // are ignored so they don't conflict with the existing values
    REQUIRE(
        buf.ToString() ==
        R"({"id":"56c031d0-24e3-4fb3-bba5-8ac53b4041d1","foo":42,"bar":"hi","x":100,"y":200})"
    );
  }

  SECTION("M define duplicate property values W reserved names not filtered out") {
    // Given an object with user-defined attributes, some of which use property names
    // that conflict with fields of our base event type
    Attribute attributes = Attribute::Object(4);
    attributes.SetObjectProperty("x", Attribute::Int(100));
    attributes.SetObjectProperty("y", Attribute::Int(200));
    attributes.SetObjectProperty("foo", Attribute::Int(300));
    attributes.SetObjectProperty("bar", Attribute::Int(400));

    // When we encode our event payload with extra attributes, with no filter func
    EncodeJsonWithMergedUserAttributes(buf.bytes, ev, obj, {attributes}, nullptr);

    // Then our conflicting property names are defined twice: this is still a
    // syntactically valid JSON value, but parsing it involves undefined behavior (and
    // in practice, the later occurrences of 'foo' and 'bar' will typically take
    // precedence)
    REQUIRE(
        buf.ToString() ==
        R"({"id":"56c031d0-24e3-4fb3-bba5-8ac53b4041d1","foo":42,"bar":"hi","x":100,"y":200,"foo":300,"bar":400})"
    );
  }
}
