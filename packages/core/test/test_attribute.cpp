// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/attribute.h"

#include <catch2/catch_test_macros.hpp>

#include "datadog/datadog_test.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

namespace {

using namespace std::string_view_literals;

using datadog::core::DatadogAttribute;
using datadog::test::GenerateRandomString;

TEST_CASE("M create attribute with int W ctor {int}", "[attribute_values]") {
  // Given
  DatadogAttribute attr{5};

  // Then
  REQUIRE(attr.type() == DatadogAttribute::Type::Int);
  REQUIRE(attr.IntValue() == 5);
}

TEST_CASE("M create attribute with uint W ctor {uint}", "[attribute_values]") {
  // Given
  DatadogAttribute attr{5u};

  // Then
  REQUIRE(attr.type() == DatadogAttribute::Type::UInt);
  REQUIRE(attr.UIntValue() == 5u);
}

TEST_CASE("M create attribute with uint W ctor {large uint}",
          "[attribute_values]") {
  // Given - uint outside of range of signed int
  DatadogAttribute attr{0xf000200500000000u};

  // Then
  REQUIRE(attr.type() == DatadogAttribute::Type::UInt);
  REQUIRE(attr.UIntValue() == 0xf000200500000000u);
  REQUIRE(attr.IntValue() == -0xFFFDFFB00000000);
}

TEST_CASE("M create attribute with double W ctor {double}",
          "[attribute_values]") {
  // Given
  DatadogAttribute attr{5.32};

  // Then
  REQUIRE(attr.type() == DatadogAttribute::Type::Double);
  REQUIRE(attr.DoubleValue() == 5.32);
}

TEST_CASE("M truncate double W IntValue / UIntValue {double}",
          "[attribute_values]") {
  // Given
  DatadogAttribute attr{5.32};

  // Then
  REQUIRE(attr.IntValue() == 5);
  REQUIRE(attr.UIntValue() == 5);
}

TEST_CASE("M return 0 for double W UIntValue {negative double}",
          "[attribute_values]") {
  // Given
  DatadogAttribute attr{-5.32};

  // Then
  REQUIRE(attr.IntValue() == -5);
  REQUIRE(attr.UIntValue() == 0);
}

TEST_CASE("M return double value for int W DoubleValue {int}",
          "[attribute_values]") {
  // Given
  DatadogAttribute attr{5};

  // Then
  REQUIRE(attr.DoubleValue() == 5.0);
}

TEST_CASE("M return double value for uint W DoubleValue {int}",
          "[attribute_values]") {
  // Given
  DatadogAttribute attr{128u};

  // Then
  REQUIRE(attr.DoubleValue() == 128.0);
}

TEST_CASE("M not overwrite old value W SetValue {copy, int}",
          "[attribute_values]") {
  // Given
  DatadogAttribute attr1{5};
  DatadogAttribute attr2{attr1};

  // When
  attr2.SetValue(25);

  // Then
  REQUIRE(attr1.type() == DatadogAttribute::Type::Int);
  REQUIRE(attr1.IntValue() == 5);
  REQUIRE(attr2.type() == DatadogAttribute::Type::Int);
  REQUIRE(attr2.IntValue() == 25);
}

TEST_CASE("M not overwrite old value W SetValue {changing types}",
          "[attribute_values]") {
  // Given
  DatadogAttribute attr1{5};
  DatadogAttribute attr2{attr1};

  // When
  attr2.SetValue(23.112);

  // Then
  REQUIRE(attr1.type() == DatadogAttribute::Type::Int);
  REQUIRE(attr1.IntValue() == 5);
  REQUIRE(attr2.type() == DatadogAttribute::Type::Double);
  REQUIRE(attr2.DoubleValue() == 23.112);
}

TEST_CASE("M create attribute with string W ctor {string_view}",
          "[attribute_values]") {
  // Given
  DatadogAttribute str("random_string");

  // When
  auto val = str.StringValue();

  // THEN
  REQUIRE(val == std::string("random_string"));
}

TEST_CASE("M use same memory in copy W copy {string}", "[attribute_cow]") {
  // Given
  DatadogAttribute str("random_string");
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
  DatadogAttribute copy{str};

  // When
  auto val = str.StringValue();
  auto copy_val = copy.StringValue();

  // THEN
  REQUIRE(val.data() == copy_val.data());
}

TEST_CASE("M not overwrite original W SetValue {string}", "[attribute_cow]") {
  // Given
  DatadogAttribute str("random_string");
  DatadogAttribute copy{str};

  // When
  copy.SetValue(GenerateRandomString());

  // THEN
  REQUIRE(str.StringValue() == "random_string"sv);
}

TEST_CASE("M create attribute with array W ctor {array}",
          "[attribute_values]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array, 10};
  array.ArraySetAt(0, DatadogAttribute{30});

  // When
  auto val = array.ArrayGetAt(0);

  // Then
  REQUIRE(val.IntValue() == 30);
}

TEST_CASE("M copy values W Reserve {array}", "[attribute_values]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array, 10};
  array.ArraySetAt(0, DatadogAttribute{30});

  // When
  array.Reserve(20);
  auto val = array.ArrayGetAt(0);

  // Then
  REQUIRE(val.IntValue() == 30);
}

TEST_CASE("M use same memory in copy W copy {array}", "[attribute_cow]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array, 10};
  array.ArraySetAt(0, DatadogAttribute{30});
  DatadogAttribute copy{array};

  // When
  const auto& val = array.ArrayGetAt(0);
  const auto& copy_val = copy.ArrayGetAt(0);

  // Then
  REQUIRE(&val == &copy_val);
}

TEST_CASE("M not overwrite original W SetValue {copy, array}",
          "[attribute_cow]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array, 10};
  array.ArraySetAt(0, DatadogAttribute{30});
  DatadogAttribute copy{array};

  // When
  const auto& val = array.ArrayGetAt(0);
  copy.ArraySetAt(0, DatadogAttribute{13});
  const auto& copy_val = copy.ArrayGetAt(0);

  // Then
  REQUIRE(&val != &copy_val);
  REQUIRE(val.IntValue() == 30);
  REQUIRE(copy_val.IntValue() == 13);
}

TEST_CASE("M not overwrite original W SetValue changes type {copy, array}",
          "[attribute_cow]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array, 10};
  array.ArraySetAt(0, DatadogAttribute{30});
  DatadogAttribute copy{array};

  // When
  const auto& val = array.ArrayGetAt(0);
  copy.ArraySetAt(0, DatadogAttribute{"random_string"});
  const auto& copy_val = copy.ArrayGetAt(0);

  // Then
  REQUIRE(&val != &copy_val);
  REQUIRE(val.IntValue() == 30);
  REQUIRE(copy_val.StringValue() == "random_string"sv);
}

TEST_CASE(
    "M keep CoW attributes in same memory in array W ArraySetAt {array, "
    "string}",
    "[attribute_cow]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array, 10};
  array.ArraySetAt(0, DatadogAttribute{GenerateRandomString()});
  DatadogAttribute copy{array};

  // When
  const auto& val = array.ArrayGetAt(0);
  const auto& copy_val = copy.ArrayGetAt(0);

  // Then
  REQUIRE(val.StringValue().data() == copy_val.StringValue().data());
}

TEST_CASE(
    "M not overwrite original CoW attributes in array W SetValue {copy, array}",
    "[attribute_cow]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array, 10};
  array.ArraySetAt(0, DatadogAttribute{"testing_string"});
  DatadogAttribute copy{array};

  // When
  copy.ArraySetAt(0, DatadogAttribute{GenerateRandomString()});

  // Then
  REQUIRE(array.ArrayGetAt(0).StringValue() == "testing_string"sv);
}

TEST_CASE(
    "M keep same memory for unchanged CoW elements in array W SetValue {copy, "
    "array}",
    "[attribute_cow]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array, 10};
  array.ArraySetAt(0, DatadogAttribute{"testing_string"});
  array.ArraySetAt(1, DatadogAttribute{GenerateRandomString()});
  DatadogAttribute copy{array};

  // When
  copy.ArraySetAt(0, DatadogAttribute{GenerateRandomString()});

  // Then

  REQUIRE(array.ArrayGetAt(1).StringValue().data() ==
          copy.ArrayGetAt(1).StringValue().data());
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

}  // namespace
