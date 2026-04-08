// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/attribute.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/util/json.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("Attribute JSON serialization", "[unit][json]") {
  static const int64_t int64_min = std::numeric_limits<int64_t>::min();
  static const int64_t int64_max = std::numeric_limits<int64_t>::max();
  static const uint64_t uint64_max = std::numeric_limits<uint64_t>::max();
  static const double double_min = std::numeric_limits<double>::min();
  static const double double_max = std::numeric_limits<double>::max();
  static const double double_inf = std::numeric_limits<double>::infinity();

  static const uint8_t uuid_bytes_zero[16] = {0};
  static const uint8_t uuid_bytes_d137[16] = {
      209, 55, 234, 75, 153, 129, 79, 158, 165, 136, 104, 200, 67, 187, 24, 156
  };

  auto array_of = [](std::initializer_list<Attribute> items) -> Attribute {
    Attribute attribute = Attribute::Array(items.size());
    for (const Attribute& item : items) {
      attribute.ArrayPush(item);
    }
    return attribute;
  };

  auto object_with =
      [](
          std::initializer_list<std::pair<std::string, Attribute>> properties
      ) -> Attribute {
    Attribute attribute = Attribute::Object(properties.size());
    for (const auto& kvp : properties) {
      attribute.SetObjectProperty(kvp.first, kvp.second);
    }
    return attribute;
  };

  struct TestParams {
    std::string_view name;
    Attribute attribute;
    std::string_view want_json;
  };
  std::vector<TestParams> tests = {
      // Literal null
      {"null", Attribute::Null(), "null"},

      // Literal bool: true or false
      {"bool (false)", Attribute::Bool(false), "false"},
      {"bool (true)", Attribute::Bool(true), "true"},

      // Literal int64_t (sign only if negative)
      {"int (zero)", Attribute::Int(0), "0"},
      {"int (negative)", Attribute::Int(-400), "-400"},
      {"int (positive)", Attribute::Int(8675309), "8675309"},
      // Limits OK
      {"int (min)", Attribute::Int(int64_min), "-9223372036854775808"},
      {"int (max)", Attribute::Int(int64_max), "9223372036854775807"},
      // Power-of-ten boundaries OK
      {"int (negative pow10 minus 1)", Attribute::Int(-100000001), "-100000001"},
      {"int (negative pow10)", Attribute::Int(-100000000), "-100000000"},
      {"int (negative pow10 plus 1)", Attribute::Int(-99999999), "-99999999"},
      {"int (pow10 minus 1)", Attribute::Int(9999999999999), "9999999999999"},
      {"int (pow10)", Attribute::Int(10000000000000), "10000000000000"},
      {"int (pow10 plus 1)", Attribute::Int(10000000000001), "10000000000001"},

      // Literal uint64_t
      {"uint (zero)", Attribute::UInt(0), "0"},
      {"uint (positive)", Attribute::UInt(2147483647), "2147483647"},
      {"uint (large)", Attribute::UInt(9223372036854775807), "9223372036854775807"},
      // Limits OK
      {"uint (max)", Attribute::UInt(uint64_max), "18446744073709551615"},
      // Power-of-ten boundaries OK
      {"uint (pow10 minus 1)",
       Attribute::UInt(9999999999999999999ull),
       "9999999999999999999"},
      {"uint (pow10)",
       Attribute::UInt(10000000000000000000ull),
       "10000000000000000000"},
      {"uint (pow10 plus 1)",
       Attribute::UInt(10000000000000000001ull),
       "10000000000000000001"},

      // Timestamp - stored as uint64 nanos since Unix epoch; formatted ISO-8601 with
      // millisecond precision (including enclosing double-quotes, as result value is
      // a JSON string literal)
      {"timestamp (zero)",
       Attribute::Timestamp(Timestamp{}),
       "\"1970-01-01T00:00:00.000Z\""},
      {"timestamp (1 microsecond)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(1000))),
       "\"1970-01-01T00:00:00.000Z\""},
      {"timestamp (1 millisecond)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(1000000))),
       "\"1970-01-01T00:00:00.001Z\""},
      {"timestamp (10 milliseconds)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(10000000))),
       "\"1970-01-01T00:00:00.010Z\""},
      {"timestamp (100 milliseconds)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(100000000))),
       "\"1970-01-01T00:00:00.100Z\""},
      {"timestamp (1 millisecond)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(1000000000))),
       "\"1970-01-01T00:00:01.000Z\""},
      {"timestamp (1 minute)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(60000000000))),
       "\"1970-01-01T00:01:00.000Z\""},
      {"timestamp (1 hour)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(3600000000000))),
       "\"1970-01-01T01:00:00.000Z\""},
      {"timestamp (1 day)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(86400000000000))),
       "\"1970-01-02T00:00:00.000Z\""},
      {"timestamp (31 days)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(2678400000000000))),
       "\"1970-02-01T00:00:00.000Z\""},
      {"timestamp (365 days)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(31536000000000000))),
       "\"1971-01-01T00:00:00.000Z\""},
      {"timestamp (leap day)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(68239266580000000))),
       "\"1972-02-29T19:21:06.580Z\""},
      {"timestamp (eve of Y2K)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(946684799999999999))),
       "\"1999-12-31T23:59:59.999Z\""},
      {"timestamp (Y2K)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(946684800000000000))),
       "\"2000-01-01T00:00:00.000Z\""},
      {"timestamp (max)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(int64_max))),
       "\"2262-04-11T23:47:16.854Z\""},
      {"timestamp (min)",
       Attribute::Timestamp(Timestamp(std::chrono::nanoseconds(int64_min))),
       "\"1677-09-21T00:12:43.145Z\""},

      // Literal double
      {"double (zero)", Attribute::Double(0.0), "0"},
      {"double (pi)", Attribute::Double(3.141592653589793), "3.141592653589793"},
      {"double (+frac)", Attribute::Double(0.123456000), "0.123456"},
      {"double (+rat)", Attribute::Double(12345.60000), "12345.6"},
      {"double (-frac)", Attribute::Double(-0.123456000), "-0.123456"},
      {"double (-rat)", Attribute::Double(-12345.60000), "-12345.6"},
      {"double (subnormal)", Attribute::Double(4.9406564584124654e-324), "5e-324"},
      {"double (big int)",
       Attribute::Double(9007199254740992.0),
       "9.007199254740992e+15"},
      // Negative zero allowed
      {"double (negative zero)", Attribute::Double(-0.0), "-0"},
      // Limits OK
      {"double (+min)", Attribute::Double(double_min), "2.2250738585072014e-308"},
      {"double (+max)", Attribute::Double(double_max), "1.7976931348623157e+308"},
      {"double (-min)", Attribute::Double(-double_min), "-2.2250738585072014e-308"},
      {"double (-max)", Attribute::Double(-double_max), "-1.7976931348623157e+308"},
      // Non-finite values converted to JSON null
      {"double (NaN)", Attribute::Double(std::nan("")), "null"},
      {"double (+inf)", Attribute::Double(double_inf), "null"},
      {"double (-inf)", Attribute::Double(-double_inf), "null"},

      // Literal UUID
      {"uuid (zero)",
       Attribute::UUID(uuid_bytes_zero),
       "\"00000000-0000-0000-0000-000000000000\""},
      {"uuid",
       Attribute::UUID(uuid_bytes_d137),
       "\"d137ea4b-9981-4f9e-a588-68c843bb189c\""},

      // Literal string
      {"string (empty)", Attribute::String(""), R"("")"},
      {"string",
       Attribute::String("that's a nice face you got there"),
       R"("that's a nice face you got there")"},
      {"string (w/ quotes)",
       // Quotes and backslashes are escaped
       Attribute::String("that's a nice \"face\" you got there"),
       R"("that's a nice \"face\" you got there")"},
      {"string (w/ backslashes)",
       Attribute::String("C:\\foo\\bar.exe"),
       R"("C:\\foo\\bar.exe")"},
      // Forward slashes are not escaped
      {"string (w/ solidus)", Attribute::String("/home/foo/bar"), R"("/home/foo/bar")"},
      // Control bytes with short escape sequences are escaped
      {"string (w/ short control codes)",
       Attribute::String("\b for backspace, \f for feed, and you know \n \r and \t"),
       R"("\b for backspace, \f for feed, and you know \n \r and \t")"},
      {"string (w/ control bytes)",
       // Other ASCII control bytes are encoded '\u00XX'
       Attribute::String("\a, \a, \a went the trolley"),
       R"("\u0007, \u0007, \u0007 went the trolley")"},
      // Multi-byte UTF-8 chars are emitted unchanged, not ASCII-escaped
      {"string (w/ non-ASCII)",
       Attribute::String("хорошо, está bien, 私の牛が戻ってきた 🐮🕺🎉"),
       R"("хорошо, está bien, 私の牛が戻ってきた 🐮🕺🎉")"},

      // Arrays
      {"array (empty)", Attribute::Array(), "[]"},
      {"array (ints)",
       array_of({Attribute::Int(1), Attribute::Int(2), Attribute::Int(3)}),
       "[1,2,3]"},
      // Mixed value types OK
      {"array (mixed values)",
       array_of(
           {Attribute::Int(-1),
            Attribute::Double(867.5309),
            Attribute::UInt(1),
            Attribute::Bool(true),
            Attribute::Null(),
            Attribute::Array(),
            Attribute::Object(),
            Attribute::String("sacré bleu")}
       ),
       "[-1,867.5309,1,true,null,[],{},\"sacré bleu\"]"},
      // Nested arrays/objects OK
      {"array (nested)",
       array_of(
           {Attribute::Int(-1),
            array_of({array_of(
                {array_of(
                     {Attribute::Int(2),
                      object_with(
                          {{"x", Attribute::Double(-33.333)},
                           {"y", Attribute::Double(double_inf)}}
                      ),
                      Attribute::Int(3)}
                 ),
                 Attribute::Bool(false)}
            )}),
            Attribute::Object()}
       ),
       "[-1,[[[2,{\"x\":-33.333,\"y\":null},3],false]],{}]"},

      // Objects
      {"object (empty)", Attribute::Object(), "{}"},
      {"object",
       object_with({
           {"my_null", Attribute::Null()},
           {"my_bool", Attribute::Bool(true)},
           {"my_int", Attribute::Int(-42)},
           {"my_uint", Attribute::UInt(999)},
           {"my_double", Attribute::Double(0.0000000000000000000000000000000000001)},
           {"my_string", Attribute::String("Frank")},
           {"my_array", Attribute::Array()},
           {"my_object", Attribute::Object()},
       }),
       R"({"my_null":null,"my_bool":true,"my_int":-42,"my_uint":999,"my_double":1e-37,"my_string":"Frank","my_array":[],"my_object":{}})"},
      // Keys are properly escaped
      {"object (escaped key)",
       object_with({{"key\\with\\\"quotes\"_and_slashes", Attribute::Int(1)}}),
       R"({"key\\with\\\"quotes\"_and_slashes":1})"},
      // Empty-string key is valid
      {"object (empty key)",
       object_with({{"", Attribute::Bool(false)}}),
       R"({"":false})"},
      // Leading whitespace in keys is valid
      {"object (space in keys)",
       object_with(
           {{" foo", Attribute::Int(11)},
            {" ", Attribute::Int(12)},
            {"\tfoo", Attribute::Int(13)}}
       ),
       R"({" foo":11," ":12,"\tfoo":13})"},
      // Nested objects/arrays OK
      {
          "object (nested)",
          object_with({
              {"items",
               array_of(
                   {object_with(
                        {{"id", Attribute::String("item-1")},
                         {"count", Attribute::Int(52)}}
                    ),
                    object_with(
                        {{"id", Attribute::String("item-2")},
                         {"count", Attribute::Int(4)}}
                    )}
               )},
              {"total", Attribute::UInt(2)},
              {"next",
               object_with(
                   {{"path", Attribute::String("/foo/bar")},
                    {"cursor", Attribute::Null()}}
               )},
          }),
          R"({"items":[{"id":"item-1","count":52},{"id":"item-2","count":4}],"total":2,"next":{"path":"/foo/bar","cursor":null}})"
      }
  };
  for (const auto& tt : tests) {
    // Given an attribute value and the known result of JSON-encoding it
    DYNAMIC_SECTION("M encode " << tt.name << " correctly") {
      if (tt.name == "object") {
        std::cout << "\n";
      }

      // When we precompute the required buffer size for our attribute value
      const size_t len = GetJsonSize(tt.attribute);

      // Then the result size is sufficient to hold the expected JSON value
      switch (tt.attribute.GetType()) {
        // For integral primitives, UUIDs, and strings, we should get the required size
        // exactly right
        case ValueType::Null:
        case ValueType::Bool:
        case ValueType::Int:
        case ValueType::UInt:
        case ValueType::UUID:
          REQUIRE(len == tt.want_json.size());
          break;

        // For everything else (i.e. doubles or compound types that might
        // contain doubles), we just assume that the precomputed size meets or
        // exceeds what WriteValue will require
        default:
          REQUIRE(len >= tt.want_json.size());
          break;
      }

      // Next: When we serialize our attribute to JSON
      std::vector<uint8_t> buffer;
      EncodeJson(buffer, tt.attribute);

      // Then our resulting bytes are an exact match for the expected JSON result
      std::string_view got_json{reinterpret_cast<char*>(buffer.data()), buffer.size()};
      REQUIRE(got_json == tt.want_json);
    }
  }
}
