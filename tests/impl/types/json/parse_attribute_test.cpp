// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/parse_attribute.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/attribute.hpp"

#include "datadog/impl/types/json.hpp"

#include "support/catch.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("ParseJsonAttribute", "[unit][json][parse_attribute]") {
  Attribute out;

  SECTION("{null}") {
    SECTION("M parse null literal") {
      REQUIRE(ParseJsonAttribute("null", out));
      REQUIRE(out.GetType() == ValueType::Null);
    }
    SECTION("M reject partial null") {
      REQUIRE_FALSE(ParseJsonAttribute("nul", out));
      REQUIRE_FALSE(ParseJsonAttribute("NULL", out));
    }
  }

  SECTION("{bool}") {
    SECTION("M parse true") {
      REQUIRE(ParseJsonAttribute("true", out));
      REQUIRE(out.GetType() == ValueType::Bool);
      REQUIRE(out.GetBoolValue() == true);
    }
    SECTION("M parse false") {
      REQUIRE(ParseJsonAttribute("false", out));
      REQUIRE(out.GetType() == ValueType::Bool);
      REQUIRE(out.GetBoolValue() == false);
    }
    SECTION("M reject non-bool") {
      REQUIRE_FALSE(ParseJsonAttribute("True", out));
      REQUIRE_FALSE(ParseJsonAttribute("yes", out));
    }
  }

  SECTION("{int}") {
    SECTION("M parse zero as Int") {
      REQUIRE(ParseJsonAttribute("0", out));
      REQUIRE(out.GetType() == ValueType::Int);
      REQUIRE(out.GetIntValue() == 0);
    }
    SECTION("M parse positive int64 as Int") {
      REQUIRE(ParseJsonAttribute("42", out));
      REQUIRE(out.GetType() == ValueType::Int);
      REQUIRE(out.GetIntValue() == 42);
    }
    SECTION("M parse negative value as Int") {
      REQUIRE(ParseJsonAttribute("-1", out));
      REQUIRE(out.GetType() == ValueType::Int);
      REQUIRE(out.GetIntValue() == -1);
    }
    SECTION("M parse max int64 as Int") {
      REQUIRE(ParseJsonAttribute("9223372036854775807", out));
      REQUIRE(out.GetType() == ValueType::Int);
      REQUIRE(out.GetIntValue() == std::numeric_limits<int64_t>::max());
    }
    SECTION("M parse large positive as UInt W value exceeds int64 max") {
      // 2^63 fits uint64 but not int64; must not be lost
      REQUIRE(ParseJsonAttribute("9223372036854775808", out));
      REQUIRE(out.GetType() == ValueType::UInt);
      REQUIRE(out.GetUIntValue() == UINT64_C(9223372036854775808));
    }
    SECTION("M parse max uint64 as UInt") {
      REQUIRE(ParseJsonAttribute("18446744073709551615", out));
      REQUIRE(out.GetType() == ValueType::UInt);
      REQUIRE(out.GetUIntValue() == std::numeric_limits<uint64_t>::max());
    }
    SECTION("M reject positive value exceeding uint64 max") {
      // Our JSON serialization code can never write numeric values of this magnitude in
      // integer format: if the value were serialized from ValueType::Double, it'd be
      // written with chars_format::general, which always uses e/E notation for
      // magnitudes of 10^6 or greater
      REQUIRE_FALSE(ParseJsonAttribute("18446744073709551616", out));
    }
    SECTION("M reject negative value exceeding int64 min") {
      REQUIRE_FALSE(ParseJsonAttribute("-9223372036854775809", out));
    }
  }

  SECTION("{double}") {
    SECTION("M parse decimal as Double") {
      REQUIRE(ParseJsonAttribute("3.14", out));
      REQUIRE(out.GetType() == ValueType::Double);
      REQUIRE(out.GetDoubleValue() > 3.0);
      REQUIRE(out.GetDoubleValue() < 4.0);
    }
    SECTION("M parse exponent notation as Double") {
      REQUIRE(ParseJsonAttribute("1e5", out));
      REQUIRE(out.GetType() == ValueType::Double);
      REQUIRE(out.GetDoubleValue() == 100000.0);
    }
    SECTION("M parse 0.0 as Double") {
      REQUIRE(ParseJsonAttribute("0.0", out));
      REQUIRE(out.GetType() == ValueType::Double);
      REQUIRE(out.GetDoubleValue() == 0.0);
    }
    SECTION("M parse integer literal as Int W no decimal or exponent") {
      // This test codifies the known limitation that type information may be lost when
      // a round-number double value is serialized: e.g. 1.0 is serialized as "1". Our
      // parsing code always parses an integer-looking value as an integer.
      REQUIRE(ParseJsonAttribute("1", out));
      REQUIRE(out.GetType() == ValueType::Int);
      RequireJsonLiteral(out, "1");
    }
  }

  SECTION("{string}") {
    SECTION("M parse plain string") {
      REQUIRE(ParseJsonAttribute(R"("hello")", out));
      REQUIRE(out.GetType() == ValueType::String);
      REQUIRE(out.GetStringValue() == "hello");
    }
    SECTION("M parse empty string") {
      REQUIRE(ParseJsonAttribute(R"("")", out));
      REQUIRE(out.GetType() == ValueType::String);
      REQUIRE(out.GetStringValue() == "");
    }
    SECTION("M unescape escape sequences in string") {
      REQUIRE(ParseJsonAttribute(R"("hello\nworld")", out));
      REQUIRE(out.GetType() == ValueType::String);
      REQUIRE(out.GetStringValue() == "hello\nworld");
    }
    SECTION("M reject truncated string literal") {
      REQUIRE_FALSE(ParseJsonAttribute(R"("unterminated)", out));
    }
    SECTION("M reject bad escape in string") {
      REQUIRE_FALSE(ParseJsonAttribute(R"("\x41")", out));
    }
  }

  SECTION("{array}") {
    SECTION("M parse empty array") {
      REQUIRE(ParseJsonAttribute("[]", out));
      REQUIRE(out.GetType() == ValueType::Array);
      REQUIRE(out.GetArrayLen() == 0);
    }
    SECTION("M parse array of ints") {
      REQUIRE(ParseJsonAttribute("[1,2,3]", out));
      REQUIRE(out.GetType() == ValueType::Array);
      REQUIRE(out.GetArrayLen() == 3);
      REQUIRE(out.GetArrayItem(0).GetIntValue() == 1);
      REQUIRE(out.GetArrayItem(1).GetIntValue() == 2);
      REQUIRE(out.GetArrayItem(2).GetIntValue() == 3);
    }
    SECTION("M parse array of mixed types") {
      REQUIRE(ParseJsonAttribute(R"([1,true,"x",null])", out));
      REQUIRE(out.GetType() == ValueType::Array);
      REQUIRE(out.GetArrayLen() == 4);
      REQUIRE(out.GetArrayItem(0).GetType() == ValueType::Int);
      REQUIRE(out.GetArrayItem(1).GetType() == ValueType::Bool);
      REQUIRE(out.GetArrayItem(2).GetType() == ValueType::String);
      REQUIRE(out.GetArrayItem(3).GetType() == ValueType::Null);
    }
    SECTION("M parse nested array") {
      REQUIRE(ParseJsonAttribute("[[1,2],[3,4]]", out));
      REQUIRE(out.GetType() == ValueType::Array);
      REQUIRE(out.GetArrayLen() == 2);
      REQUIRE(out.GetArrayItem(0).GetArrayLen() == 2);
      REQUIRE(out.GetArrayItem(1).GetArrayLen() == 2);
    }
    SECTION("M reject unclosed array") {
      REQUIRE_FALSE(ParseJsonAttribute("[1,2", out));
    }
    SECTION("M reject trailing comma in array") {
      REQUIRE_FALSE(ParseJsonAttribute("[1,]", out));
    }
    SECTION("M reject adjacent elements without a comma") {
      // [1true] and [{}[]] have no comma between adjacent values and are not valid JSON
      REQUIRE_FALSE(ParseJsonAttribute("[1true]", out));
      REQUIRE_FALSE(ParseJsonAttribute("[{}[]]", out));
      REQUIRE_FALSE(ParseJsonAttribute("[1 2]", out));
    }
  }

  SECTION("{object}") {
    SECTION("M parse empty object") {
      REQUIRE(ParseJsonAttribute("{}", out));
      REQUIRE(out.GetType() == ValueType::Object);
      REQUIRE(out.GetObjectPropertyCount() == 0);
    }
    SECTION("M parse object with scalar properties") {
      REQUIRE(ParseJsonAttribute(R"({"a":1,"b":true,"c":"hello"})", out));
      REQUIRE(out.GetType() == ValueType::Object);
      REQUIRE(out.GetObjectPropertyCount() == 3);
      REQUIRE(out.GetObjectProperty("a").GetIntValue() == 1);
      REQUIRE(out.GetObjectProperty("b").GetBoolValue() == true);
      REQUIRE(out.GetObjectProperty("c").GetStringValue() == "hello");
    }
    SECTION("M parse nested object") {
      REQUIRE(ParseJsonAttribute(R"({"x":{"y":42}})", out));
      REQUIRE(out.GetType() == ValueType::Object);
      REQUIRE(out.GetObjectProperty("x").GetObjectProperty("y").GetIntValue() == 42);
    }
    SECTION("M reject unclosed object") {
      REQUIRE_FALSE(ParseJsonAttribute(R"({"a":1)", out));
    }
    SECTION("M reject trailing comma in object") {
      REQUIRE_FALSE(ParseJsonAttribute(R"({"a":1,})", out));
      REQUIRE_FALSE(ParseJsonAttribute(R"({"a":1,"b":2,})", out));
    }
    SECTION("M reject malformed object - bad value") {
      REQUIRE_FALSE(ParseJsonAttribute(R"({"a":bad})", out));
    }
  }

  SECTION("{trailing tokens}") {
    // Any suffix after a complete, valid JSON value must be rejected; the function
    // contracts to parse the entire input string as a single JSON value
    SECTION("M reject trailing tokens after null") {
      REQUIRE_FALSE(ParseJsonAttribute("nullgarbage", out));
      REQUIRE_FALSE(ParseJsonAttribute("null,null", out));
    }
    SECTION("M reject trailing tokens after bool") {
      REQUIRE_FALSE(ParseJsonAttribute("true,false", out));
      REQUIRE_FALSE(ParseJsonAttribute("trueX", out));
    }
    SECTION("M reject trailing tokens after number") {
      REQUIRE_FALSE(ParseJsonAttribute("42junk", out));
      REQUIRE_FALSE(ParseJsonAttribute("3.14abc", out));
    }
    SECTION("M reject trailing tokens after string") {
      REQUIRE_FALSE(ParseJsonAttribute(R"("hello"world)", out));
      REQUIRE_FALSE(ParseJsonAttribute(R"("x","y")", out));
    }
    SECTION("M reject trailing tokens after array") {
      REQUIRE_FALSE(ParseJsonAttribute("[1]junk", out));
      REQUIRE_FALSE(ParseJsonAttribute("[]null", out));
    }
    SECTION("M reject trailing tokens after object") {
      REQUIRE_FALSE(ParseJsonAttribute(R"({}null)", out));
      REQUIRE_FALSE(ParseJsonAttribute(R"({"a":1}X)", out));
    }
  }
}

TEST_CASE("ParseJsonAttribute round-trip fidelity", "[unit][json][parse_attribute]") {
  // Given the same kitchen-sink JSON test value used in Attribute API tests
  static constexpr std::string_view KITCHEN_SINK_JSON =
      R"({"process":{"pid":9238451,"guid":"ccb79084-bc2b-4549-bbc7-f27e153fd4b6","name":"my-cool-program","args":["--mode","good"],"started_at":"1974-08-09T16:00:00.000Z"},"state":{"rect":[[0.03333,-12.3],[94,98.7001]],"state":[{}],"offset":-1,"active":true},"tags":["blue","meh",null]})";

  // When we parse that value into an Attribute
  Attribute parsed;
  REQUIRE(ParseJsonAttribute(KITCHEN_SINK_JSON, parsed));

  // Then our value is successfully parsed
  REQUIRE(
      parsed.GetObjectProperty("process").GetObjectProperty("name").GetStringValue() ==
      "my-cool-program"
  );
  REQUIRE(parsed.GetObjectProperty("tags").GetArrayLen() == 3);
  REQUIRE(
      parsed.GetObjectProperty("tags").GetArrayItem(2).GetType() == ValueType::Null
  );

  // And re-serializing it produces an identical JSON string
  RequireJsonLiteral(parsed, KITCHEN_SINK_JSON);
}
