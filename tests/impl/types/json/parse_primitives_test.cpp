// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/parse_primitives.hpp"

#include <cstdint>
#include <limits>
#include <string>

#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("ParseJsonString", "[unit][json][parse_primitives]") {
  std::string out;

  SECTION("M parse plain string") {
    REQUIRE(ParseJsonString("\"hello\"", out));
    REQUIRE(out == "hello");
  }

  SECTION("M parse empty string") {
    REQUIRE(ParseJsonString("\"\"", out));
    REQUIRE(out == "");
  }

  SECTION("M parse single-character escapes") {
    REQUIRE(ParseJsonString("\"\\\"\"", out));
    REQUIRE(out == "\"");
    REQUIRE(ParseJsonString("\"\\\\\"", out));
    REQUIRE(out == "\\");
    REQUIRE(ParseJsonString("\"\\b\"", out));
    REQUIRE(out == "\b");
    REQUIRE(ParseJsonString("\"\\f\"", out));
    REQUIRE(out == "\f");
    REQUIRE(ParseJsonString("\"\\n\"", out));
    REQUIRE(out == "\n");
    REQUIRE(ParseJsonString("\"\\r\"", out));
    REQUIRE(out == "\r");
    REQUIRE(ParseJsonString("\"\\t\"", out));
    REQUIRE(out == "\t");
    REQUIRE(ParseJsonString("\"\\/\"", out));
    REQUIRE(out == "/");
  }

  SECTION("M parse \\u00XX escape sequences") {
    // \u0007 is BEL
    REQUIRE(ParseJsonString("\"\\u0007\"", out));
    REQUIRE(out == "\x07");
    // \u001f is unit separator
    REQUIRE(ParseJsonString("\"\\u001f\"", out));
    REQUIRE(out == "\x1f");
    // uppercase hex digits
    REQUIRE(ParseJsonString("\"\\u00AB\"", out));
    REQUIRE(out == "\xab");
  }

  SECTION("M reject \\uXXXX where first two digits are not 00") {
    REQUIRE_FALSE(ParseJsonString("\"\\u0100\"", out));
    REQUIRE_FALSE(ParseJsonString("\"\\u1234\"", out));
  }

  SECTION("M reject invalid escape character") {
    REQUIRE_FALSE(ParseJsonString("\"\\x41\"", out));
  }

  SECTION("M reject truncated escape") {
    REQUIRE_FALSE(ParseJsonString("\"\\\"", out));
    REQUIRE_FALSE(ParseJsonString("\"\\u00\"", out));
  }

  SECTION("M reject missing quotes") {
    REQUIRE_FALSE(ParseJsonString("hello", out));
    REQUIRE_FALSE(ParseJsonString("\"hello", out));
    REQUIRE_FALSE(ParseJsonString("hello\"", out));
  }

  SECTION("M pass through multi-byte UTF-8 unchanged") {
    REQUIRE(ParseJsonString("\"хорошо\"", out));
    REQUIRE(out == "хорошо");
  }

  SECTION("M reject unescaped control bytes (0x00-0x1f)") {
    // JSON requires control characters to be escaped; a raw byte below 0x20 inside a
    // string literal is not valid JSON and must be rejected
    REQUIRE_FALSE(ParseJsonString("\"\x01\"", out));  // SOH
    REQUIRE_FALSE(ParseJsonString("\"\x09\"", out));  // HT (raw tab, not \t)
    REQUIRE_FALSE(ParseJsonString("\"\x0a\"", out));  // LF (raw newline, not \n)
    REQUIRE_FALSE(ParseJsonString("\"\x0d\"", out));  // CR
    REQUIRE_FALSE(ParseJsonString("\"\x1f\"", out));  // US
    REQUIRE_FALSE(ParseJsonString("\"first\x0asecond\"", out));  // embedded LF
  }
}

TEST_CASE("ParseJsonUUID", "[unit][json][parse_primitives]") {
  UUID out{};

  SECTION("M parse valid UUID") {
    REQUIRE(ParseJsonUUID("\"a991ca10-4004-4004-4004-beefbeefbeef\"", out));
    REQUIRE(out == *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef"));
  }

  SECTION("M reject wrong length") {
    REQUIRE_FALSE(ParseJsonUUID("\"a991ca10-4004-4004-4004-beefbeefbee\"", out));
    REQUIRE_FALSE(ParseJsonUUID("\"a991ca10-4004-4004-4004-beefbeefbeef0\"", out));
  }

  SECTION("M reject missing quotes") {
    REQUIRE_FALSE(ParseJsonUUID("a991ca10-4004-4004-4004-beefbeefbeef", out));
  }

  SECTION("M reject malformed UUID content") {
    REQUIRE_FALSE(ParseJsonUUID("\"a991ca10-4004-4004-4004-beefbeefbegg\"", out));
  }
}

TEST_CASE("ParseJsonUInt64", "[unit][json][parse_primitives]") {
  uint64_t out{};

  SECTION("M parse zero") {
    REQUIRE(ParseJsonUInt64("0", out));
    REQUIRE(out == 0);
  }

  SECTION("M parse max uint64") {
    constexpr uint64_t max_u64 = std::numeric_limits<uint64_t>::max();
    REQUIRE(ParseJsonUInt64("18446744073709551615", out));
    REQUIRE(out == max_u64);
  }

  SECTION("M parse ordinary value") {
    REQUIRE(ParseJsonUInt64("12345", out));
    REQUIRE(out == 12345);
  }

  SECTION("M reject negative values") { REQUIRE_FALSE(ParseJsonUInt64("-1", out)); }

  SECTION("M reject non-numeric") { REQUIRE_FALSE(ParseJsonUInt64("abc", out)); }

  SECTION("M reject float literal") { REQUIRE_FALSE(ParseJsonUInt64("1.5", out)); }

  SECTION("M reject overflow") {
    REQUIRE_FALSE(ParseJsonUInt64("18446744073709551616", out));
  }
}

TEST_CASE("ParseJsonInt64", "[unit][json][parse_primitives]") {
  int64_t out{};

  SECTION("M parse zero") {
    REQUIRE(ParseJsonInt64("0", out));
    REQUIRE(out == 0);
  }

  SECTION("M parse positive max") {
    constexpr int64_t max_i64 = std::numeric_limits<int64_t>::max();
    REQUIRE(ParseJsonInt64("9223372036854775807", out));
    REQUIRE(out == max_i64);
  }

  SECTION("M parse negative min") {
    constexpr int64_t min_i64 = std::numeric_limits<int64_t>::min();
    REQUIRE(ParseJsonInt64("-9223372036854775808", out));
    REQUIRE(out == min_i64);
  }

  SECTION("M parse negative value") {
    REQUIRE(ParseJsonInt64("-42", out));
    REQUIRE(out == -42);
  }

  SECTION("M reject non-numeric") { REQUIRE_FALSE(ParseJsonInt64("abc", out)); }

  SECTION("M reject float literal") { REQUIRE_FALSE(ParseJsonInt64("1.5", out)); }

  SECTION("M reject values that fit uint64 but not int64") {
    // 2^63 fits uint64 but not int64
    REQUIRE_FALSE(ParseJsonInt64("9223372036854775808", out));
  }
}

TEST_CASE("ParseJsonDouble", "[unit][json][parse_primitives]") {
  double out{};

  SECTION("M parse integer-looking value") {
    REQUIRE(ParseJsonDouble("1", out));
    REQUIRE(out == 1.0);
  }

  SECTION("M parse decimal value") {
    REQUIRE(ParseJsonDouble("3.14", out));
    REQUIRE(out > 3.0);
    REQUIRE(out < 4.0);
  }

  SECTION("M parse exponent notation") {
    REQUIRE(ParseJsonDouble("1e5", out));
    REQUIRE(out == 100000.0);
  }

  SECTION("M parse zero") {
    REQUIRE(ParseJsonDouble("0.0", out));
    REQUIRE(out == 0.0);
  }

  SECTION("M parse large integer") {
    REQUIRE(ParseJsonDouble("100", out));
    REQUIRE(out == 100.0);
  }

  SECTION("M reject non-numeric") { REQUIRE_FALSE(ParseJsonDouble("abc", out)); }

  SECTION("M reject quoted value") { REQUIRE_FALSE(ParseJsonDouble("\"1.0\"", out)); }

  SECTION("M reject empty string") { REQUIRE_FALSE(ParseJsonDouble("", out)); }

  SECTION("M reject leading whitespace") {
    // strtod_l accepts leading whitespace; JSON numbers must not have it
    REQUIRE_FALSE(ParseJsonDouble(" 1.0", out));
    REQUIRE_FALSE(ParseJsonDouble("\t3.14", out));
  }

  SECTION("M reject hex float literals") {
    // strtod_l accepts C99 hex floats (e.g. 0x1.fp3); JSON does not
    REQUIRE_FALSE(ParseJsonDouble("0x1p0", out));
    REQUIRE_FALSE(ParseJsonDouble("0x1.fp3", out));
  }

  SECTION("M reject nan and inf") {
    // strtod_l accepts nan/inf as special values; JSON does not
    REQUIRE_FALSE(ParseJsonDouble("nan", out));
    REQUIRE_FALSE(ParseJsonDouble("NaN", out));
    REQUIRE_FALSE(ParseJsonDouble("inf", out));
    REQUIRE_FALSE(ParseJsonDouble("infinity", out));
    REQUIRE_FALSE(ParseJsonDouble("-inf", out));
  }
}

TEST_CASE("ParseJsonBool", "[unit][json][parse_primitives]") {
  bool out{};

  SECTION("M parse true") {
    REQUIRE(ParseJsonBool("true", out));
    REQUIRE(out == true);
  }

  SECTION("M parse false") {
    REQUIRE(ParseJsonBool("false", out));
    REQUIRE(out == false);
  }

  SECTION("M reject non-boolean value") {
    REQUIRE_FALSE(ParseJsonBool("", out));
    REQUIRE_FALSE(ParseJsonBool("yes", out));
    REQUIRE_FALSE(ParseJsonBool("1", out));
    REQUIRE_FALSE(ParseJsonBool("True", out));
    REQUIRE_FALSE(ParseJsonBool("FALSE", out));
  }
}
