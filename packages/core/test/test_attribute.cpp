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

TEST_CASE("M create object W ctor", "[attribute_values]") {
  // Given
  DatadogAttribute object(DatadogAttribute::Type::Object, 5);

  // Then
  REQUIRE(object.type() == DatadogAttribute::Type::Object);
}

TEST_CASE("M add value for key W SetMember", "[attribute_values]") {
  // Given
  DatadogAttribute object(DatadogAttribute::Type::Object, 5);

  // When
  object.SetMember("member", DatadogAttribute{"random_string"});

  // Then
  REQUIRE(object.GetMember("member").StringValue() == "random_string"sv);
}

TEST_CASE("M replace value for key W SetMember {existing key}",
          "[attribute_values]") {
  // Given
  DatadogAttribute object(DatadogAttribute::Type::Object, 5);
  object.SetMember("member", DatadogAttribute{"random_string"});

  // When
  object.SetMember("member", DatadogAttribute{55});

  // Then
  REQUIRE(object.GetMember("member").IntValue() == 55);
}

TEST_CASE("M expand capacity for value W SetMember {capacity}",
          "[attribute_values]") {
  // Given
  DatadogAttribute object(DatadogAttribute::Type::Object, 2);

  // When
  object.SetMember("member_1", DatadogAttribute{GenerateRandomString()});
  object.SetMember("member_2", DatadogAttribute{GenerateRandomString()});
  object.SetMember("member_3", DatadogAttribute{22145.223});

  // Then
  REQUIRE(object.GetMember("member_3").DoubleValue() == 22145.223);
}

TEST_CASE("M not overwrite copy W SetMember", "[attribute_cow]") {
  // Given
  DatadogAttribute object(DatadogAttribute::Type::Object, 2);
  object.SetMember("member_1", DatadogAttribute{GenerateRandomString()});
  DatadogAttribute copy{object};

  // When
  object.SetMember("member_1", DatadogAttribute{GenerateRandomString()});

  // Then
  REQUIRE(object.GetMember("member_1").StringValue() !=
          copy.GetMember("member_1").StringValue());
}

TEST_CASE(
    "M keep unmodified members in same memory addresses W SetMember {strings}",
    "[attribute_cow]") {
  // Given
  DatadogAttribute object(DatadogAttribute::Type::Object, 2);
  object.SetMember("member_1", DatadogAttribute{GenerateRandomString()});
  object.SetMember("member_2", DatadogAttribute{GenerateRandomString()});
  DatadogAttribute copy{object};

  // When
  object.SetMember("member_1", DatadogAttribute{GenerateRandomString()});

  // Then
  const auto& member_2 = object.GetMember("member_2");
  const auto& member_2_copy = copy.GetMember("member_2");
  REQUIRE(member_2.StringValue().data() == member_2_copy.StringValue().data());
}

class ComplexObjectTestFixture {
 public:
  ComplexObjectTestFixture()
      : complex_object_{DatadogAttribute::Type::Object, 3} {
    DatadogAttribute sub_object(DatadogAttribute::Type::Object, 2);
    sub_object.SetMember("sub_member_1", DatadogAttribute{"sub_object_value"});
    sub_object.SetMember("sub_member_2", DatadogAttribute{915});

    complex_object_.SetMember("object_member", sub_object);
    complex_object_.SetMember("string_member",
                              DatadogAttribute{"string_value"});
    complex_object_.SetMember("prim_member", DatadogAttribute{12166});
  }

  DatadogAttribute complex_object_;
};

TEST_CASE_METHOD(ComplexObjectTestFixture,
                 "M allow complex objects W SetMember",
                 "[attribute_cow]") {
  // Then
  REQUIRE(complex_object_.GetMember("object_member")
              .GetMember("sub_member_1")
              .StringValue() == "sub_object_value"sv);
  REQUIRE(complex_object_.GetMember("object_member")
              .GetMember("sub_member_2")
              .IntValue() == 915);
}

TEST_CASE_METHOD(ComplexObjectTestFixture,
                 "M not modify original W SetMember {nested object}",
                 "[attribute_cow]") {
  // Given
  DatadogAttribute complex_copy{complex_object_};

  // When
  auto sub_object = complex_object_.GetMember("object_member");
  sub_object.SetMember("sub_member_1", DatadogAttribute{"new_value"});
  sub_object.SetMember("sub_member_3", DatadogAttribute{222.451});
  complex_object_.SetMember("object_member", sub_object);

  // Then
  REQUIRE(complex_copy.GetMember("object_member")
              .GetMember("sub_member_1")
              .StringValue() == "sub_object_value"sv);
  REQUIRE(complex_copy.GetMember("object_member")
              .GetMember("sub_member_2")
              .IntValue() == 915);
  REQUIRE(complex_copy.GetMember("object_member")
              .GetMember("sub_member_3")
              .type() == DatadogAttribute::Type::Null);
  REQUIRE(complex_object_.GetMember("object_member")
              .GetMember("sub_member_1")
              .StringValue() == "new_value"sv);
  REQUIRE(complex_object_.GetMember("object_member")
              .GetMember("sub_member_2")
              .IntValue() == 915);
  REQUIRE(complex_object_.GetMember("object_member")
              .GetMember("sub_member_3")
              .DoubleValue() == 222.451);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

}  // namespace
