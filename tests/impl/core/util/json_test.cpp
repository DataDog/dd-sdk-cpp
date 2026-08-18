// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/util/json.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <string_view>
#include <vector>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/events/omissible.hpp"
#include "datadog/impl/core/events/struct.hpp"

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

struct JsonVectorEvent {
  OmitIfEmpty<std::vector<JsonTestEvent>> objects;
};
DATADOG_JSON_STRUCT(JsonVectorEvent, DATADOG_JSON_FIELD(objects))
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

TEST_CASE("TryEncodeJson", "[unit][json]") {
  SECTION("M write value to buffer W buffer is exactly GetJsonSize bytes") {
    // Given a simple integer value and a buffer exactly sized to fit it
    const uint64_t value = 12345;
    const size_t required = GetJsonSize(value);
    std::vector<char> buf(required, '\0');

    // When we call TryEncodeJson
    const auto result = TryEncodeJson(buf.data(), buf.size(), value);

    // Then it returns the byte count and the buffer contains the expected JSON
    REQUIRE(result.has_value());
    REQUIRE(*result == buf.size());
    REQUIRE(std::string_view(buf.data(), buf.size()) == "12345");
  }

  SECTION("M return false W buffer is one byte smaller than required") {
    // Given a simple integer value and a buffer one byte too small
    const uint64_t value = 12345;
    const size_t required = GetJsonSize(value);
    REQUIRE(required > 0);
    std::vector<char> buf(required - 1, 'X');
    const std::vector<char> original = buf;

    // When we call TryEncodeJson
    const auto result = TryEncodeJson(buf.data(), buf.size(), value);

    // Then it returns nullopt and the buffer is not modified
    REQUIRE_FALSE(result.has_value());
    REQUIRE(buf == original);
  }

  SECTION("M return false W buffer size is zero") {
    // Given any value and a zero-size buffer (no value encodes to zero bytes)
    const uint64_t value = 12345;
    char c = 'X';
    const auto result = TryEncodeJson(&c, 0, value);

    // Then it returns nullopt and the byte at the pointer is not modified
    REQUIRE_FALSE(result.has_value());
    REQUIRE(c == 'X');
  }

  SECTION("M write struct value to buffer W buffer is exactly GetJsonSize bytes") {
    // Given a struct value
    JsonTestEvent ev{*UUID::Parse("56c031d0-24e3-4fb3-bba5-8ac53b4041d1"), 42, "hi"};
    const size_t required = GetJsonSize(ev);
    std::vector<char> buf(required, '\0');

    // When we call TryEncodeJson
    const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

    // Then it returns the byte count and produces the expected JSON
    REQUIRE(result.has_value());
    REQUIRE(*result == buf.size());
    REQUIRE(
        std::string_view(buf.data(), buf.size()) ==
        R"({"id":"56c031d0-24e3-4fb3-bba5-8ac53b4041d1","foo":42,"bar":"hi"})"
    );
  }

  SECTION("M return false for struct W buffer is one byte smaller than required") {
    // Given a struct value and a buffer one byte too small
    JsonTestEvent ev{*UUID::Parse("56c031d0-24e3-4fb3-bba5-8ac53b4041d1"), 42, "hi"};
    const size_t required = GetJsonSize(ev);
    REQUIRE(required > 0);
    std::vector<char> buf(required - 1, 'X');
    const std::vector<char> original = buf;

    // When we call TryEncodeJson
    const auto result = TryEncodeJson(buf.data(), buf.size(), ev);

    // Then it returns nullopt and the buffer is not modified
    REQUIRE_FALSE(result.has_value());
    REQUIRE(buf == original);
  }
}

TEST_CASE("WriteJson {std::vector}", "[unit][json]") {
  JsonBuffer buf;

  SECTION("M serialize multiple elements as a JSON array") {
    // std::vector<std::string> is encoded as a JSON array of JSON string literals
    std::vector<std::string> v{"a", "b", "c"};
    EncodeJson(buf.bytes, v);
    REQUIRE(buf.ToString() == R"(["a","b","c"])");
  }

  SECTION("M serialize a single element as a JSON array") {
    // Single-element std::vector<std::string> is encoded as a single-item array
    std::vector<std::string> v{"a"};
    EncodeJson(buf.bytes, v);
    REQUIRE(buf.ToString() == R"(["a"])");
  }

  SECTION("M serialize an empty vector as an empty JSON array") {
    // Empty std::vector<T> is encoded as an empty JSON array literal
    std::vector<std::string> v{};
    EncodeJson(buf.bytes, v);
    REQUIRE(buf.ToString() == "[]");
  }

  SECTION(
      "M serialize std::vector<T> as a JSON array of JSON objects W T is a "
      "JSON-serializable struct type"
  ) {
    // Non-empty std::vector<JsonTestEvent> is encoded as a JSON array containing JSON
    // objects serialized from item values
    JsonVectorEvent ev{std::vector<JsonTestEvent>{
        {*UUID::Parse("56c031d0-24e3-4fb3-bba5-8ac53b4041d1"), 42, "hi"}
    }};
    EncodeJson(buf.bytes, ev);
    REQUIRE(
        buf.ToString() ==
        R"({"objects":[{"id":"56c031d0-24e3-4fb3-bba5-8ac53b4041d1","foo":42,"bar":"hi"}]})"
    );
  }

  SECTION("M omit OmitIfEmpty vector field W vector is empty") {
    // Empty OmitIfEmpty<std::vector<T>> is skipped from struct encoding
    JsonVectorEvent ev{};
    EncodeJson(buf.bytes, ev);
    REQUIRE(buf.ToString() == "{}");
  }
}
