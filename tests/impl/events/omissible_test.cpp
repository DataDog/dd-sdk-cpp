// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "events/omissible.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <optional>
#include <string>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"
#include "events/struct.hpp"
#include "events/timestamp.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

/**
 * Basic struct used to test serialization of Omissible<T> within a JSON object.
 */
struct SomeStruct {
  Omissible<int64_t> i64{0};
  Omissible<std::string> str;
  Omissible<IsoTimestamp> time;
  Omissible<UUID> uid;
};

DATADOG_JSON_STRUCT(
    SomeStruct,
    DATADOG_JSON_FIELD(i64),
    DATADOG_JSON_FIELD(str),
    DATADOG_JSON_FIELD(time),
    DATADOG_JSON_FIELD(uid)
)

namespace datadog::impl {
/**
 * Contrived wrapper for an int that's only serialized as a JSON object property when
 * its value is even.
 */
struct OnlyEven {
  int x;
};
bool HasJsonValue(const Omissible<OnlyEven>& value) { return value.value.x % 2 == 0; }
size_t GetJsonSize(const OnlyEven& value) { return GetJsonSize(value.x); }
size_t WriteJson(char* dst, size_t n, const OnlyEven& value) {
  return WriteJson(dst, n, value.x);
}
}  // namespace datadog::impl

/**
 * Struct allowing us to test serialization of Omissible<OnlyEven> members.
 */
struct EvenCoords {
  Omissible<OnlyEven> x;
  Omissible<OnlyEven> y;
  Omissible<OnlyEven> z;
};
DATADOG_JSON_STRUCT(
    EvenCoords, DATADOG_JSON_FIELD(x), DATADOG_JSON_FIELD(y), DATADOG_JSON_FIELD(z)
)

TEST_CASE("omissible JSON serialization", "[unit][events]") {
  SECTION("M omit struct members from serialized JSON object W values are default") {
    // With all Omissible<T> fields at zero, we get an empty object
    SomeStruct s;
    RequireJsonValue(s, "{}");

    // After setting a few fields, only those fields are present
    s.str = "hello";
    s.time = Timestamp{std::chrono::nanoseconds(946684799999999999)};
    s.uid = *UUID::Parse("a07789cb-4e46-4c36-9f73-70e8606336e0");
    RequireJsonValue(
        s,
        R"({"str":"hello","time":"1999-12-31T23:59:59.999Z","uid":"a07789cb-4e46-4c36-9f73-70e8606336e0"})"
    );

    // After clearing some of those fields and setting another one, the set of
    // properties present in the JSON object changes accordingly
    s.str.value.clear();
    s.time = Timestamp{};
    s.i64 = 42;
    RequireJsonValue(s, R"({"i64":42,"uid":"a07789cb-4e46-4c36-9f73-70e8606336e0"})");
  }

  SECTION("M use type-specific omission criteria W HasJsonValue overloaded") {
    EvenCoords coords{OnlyEven{16}, OnlyEven{17}, OnlyEven{18}};
    RequireJsonValue(coords, R"({"x":16,"z":18})");
  }

  SECTION("M have no effect W used alone") {
    // Omissible<T> overloads HasJsonValue(), which our DATADOG_JSON_STRUCT
    // implementation uses in order to determine whether a value should be included: it
    // has no effect on how the value is serialized to JSON on its own
    Omissible<int> zero = 0;
    RequireJsonValue(zero, "0");
  }
}
