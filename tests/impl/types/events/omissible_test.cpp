// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/events/omissible.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <optional>
#include <string>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/types/events/struct.hpp"
#include "datadog/impl/types/events/timestamp.hpp"

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
  Omissible<std::optional<bool>> opt_bool;
};

DATADOG_JSON_STRUCT(
    SomeStruct,
    DATADOG_JSON_FIELD(i64),
    DATADOG_JSON_FIELD(str),
    DATADOG_JSON_FIELD(time),
    DATADOG_JSON_FIELD(uid),
    DATADOG_JSON_FIELD(opt_bool)
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

TEST_CASE("Omissible JSON serialization", "[unit][events]") {
  SECTION("M omit struct members from serialized JSON object W values are default") {
    // With all Omissible<T> fields at zero, we get an empty object
    SomeStruct s;
    RequireJsonLiteral(s, "{}");

    // After setting a few fields, only those fields are present
    s.str = "hello";
    s.time = Timestamp{std::chrono::nanoseconds(946684799999999999)};
    s.uid = *UUID::Parse("a07789cb-4e46-4c36-9f73-70e8606336e0");
    RequireJsonLiteral(
        s,
        R"({"str":"hello","time":"1999-12-31T23:59:59.999Z","uid":"a07789cb-4e46-4c36-9f73-70e8606336e0"})"
    );

    // After clearing some of those fields and setting another one, the set of
    // properties present in the JSON object changes accordingly
    s.str.value.clear();
    s.time = Timestamp{};
    s.i64 = 42;
    RequireJsonLiteral(s, R"({"i64":42,"uid":"a07789cb-4e46-4c36-9f73-70e8606336e0"})");
  }

  SECTION("M allow implicit conversion from std::string_view W type is std::string") {
    struct Hi {
      Omissible<std::string> s;
      Hi(std::string_view in_s) : s(in_s) {}
    };
    Hi hi{"hello"};
    REQUIRE(hi.s.value == "hello");
  }

  SECTION("M use type-specific omission criteria W HasJsonValue overloaded") {
    EvenCoords coords{OnlyEven{16}, OnlyEven{17}, OnlyEven{18}};
    RequireJsonLiteral(coords, R"({"x":16,"z":18})");
  }

  SECTION("M treat zero value as significant W value is wrapped in std::optional") {
    // Given a struct with an Omissible<std::optional<bool>> value
    SomeStruct s;

    // When we serialize the struct while opt_bool holds true, Then we get JSON true
    s.opt_bool.value = true;
    RequireJsonLiteral(s, R"({"opt_bool":true})");

    // When we serialize the struct while opt_bool holds false, Then we get JSON false
    s.opt_bool.value = false;
    RequireJsonLiteral(s, R"({"opt_bool":false})");

    // When we serialize the struct while opt_bool is nullopt, we get no value
    s.opt_bool.value.reset();
    RequireJsonLiteral(s, R"({})");
  }

  SECTION("M have no effect W used alone") {
    // Omissible<T> overloads HasJsonValue(), which our DATADOG_JSON_STRUCT
    // implementation uses in order to determine whether a value should be included: it
    // has no effect on how the value is serialized to JSON on its own
    Omissible<int> zero = 0;
    RequireJsonLiteral(zero, "0");
  }
}

/**
 * Struct allowing us to test Omissible<T> (a.k.a. OmitIfZero/OmitIfFalse/OmitIfEmpty)
 * vs. Omissible<std::optional<T>> (a.k.a. OmitIfNoValue).
 */
struct OmissibleAliases {
  // Aliases to make type declarations clearer
  OmitIfZero<UUID> uuid_if_nonzero;
  OmitIfFalse<bool> bool_if_true{false};
  OmitIfEmpty<std::string> string_if_non_empty;

  OmitIfNoValue<UUID> uuid_if_set;
  OmitIfNoValue<bool> bool_if_set;
  OmitIfNoValue<std::string> string_if_set;
};
DATADOG_JSON_STRUCT(
    OmissibleAliases,
    DATADOG_JSON_FIELD(uuid_if_nonzero),
    DATADOG_JSON_FIELD(bool_if_true),
    DATADOG_JSON_FIELD(string_if_non_empty),
    DATADOG_JSON_FIELD(uuid_if_set),
    DATADOG_JSON_FIELD(bool_if_set),
    DATADOG_JSON_FIELD(string_if_set)
)

TEST_CASE("OmitIf* JSON serialization", "[unit][events]") {
  // Given a default value
  OmissibleAliases value;
  SECTION("M omit all properties at default values") {
    RequireJsonLiteral(value, "{}");
  }

  SECTION("M render properties W values wrapped in OmitIfZero are nonzero") {
    // When we make a subset of OmitIfZero/OmitIfFalse/OmitIfEmpty values nonzero
    value.uuid_if_nonzero = *UUID::Parse("d243787d-44af-4be1-b627-06e0ea68cea0");
    value.bool_if_true = true;

    // Then those members are reflected as JSON object properties
    RequireJsonLiteral(
        value,
        R"({"uuid_if_nonzero":"d243787d-44af-4be1-b627-06e0ea68cea0","bool_if_true":true})"
    );

    // Next: When we reset a value to zero and set another one to a non-default value
    value.uuid_if_nonzero = UUID::Zero;
    value.string_if_non_empty = "hello";

    // Then we get the expected set of JSON properties
    RequireJsonLiteral(value, R"({"bool_if_true":true,"string_if_non_empty":"hello"})");
  }

  SECTION("M render properties W values wrapped in OmitIfNoValue are non-nullopt") {
    // When we assign any values to our OmitIfNoValue members
    value.uuid_if_set = *UUID::Parse("d243787d-44af-4be1-b627-06e0ea68cea0");
    value.bool_if_set = true;
    value.string_if_set = "hello";

    // Then all of those members are reflected as JSON properties
    RequireJsonLiteral(
        value,
        R"({"uuid_if_set":"d243787d-44af-4be1-b627-06e0ea68cea0","bool_if_set":true,"string_if_set":"hello"})"
    );

    // Next: When we assign zero values to those same members
    value.uuid_if_set = UUID::Zero;
    value.bool_if_set = false;
    value.string_if_set = "";

    // Then those properties are still present with significant zero values
    RequireJsonLiteral(
        value,
        R"({"uuid_if_set":"00000000-0000-0000-0000-000000000000","bool_if_set":false,"string_if_set":""})"
    );

    // Next: When we reset any subset of those members to nullopt
    value.uuid_if_set.value.reset();
    value.bool_if_set.value.reset();

    // Then their corresponding properties are no longer present in the JSON object
    RequireJsonLiteral(value, R"({"string_if_set":""})");
  }
}
