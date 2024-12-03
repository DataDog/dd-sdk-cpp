// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/attribute.h"

#include <catch2/catch_test_macros.hpp>

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

namespace {

using datadog::core::DatadogAttribute;

TEST_CASE("M create attribute with int W ctor", "[attribute_values]") {
  DatadogAttribute attr{5};

  REQUIRE(attr.type() == DatadogAttribute::Type::Int);
  REQUIRE(attr.IntValue() == 5);
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

TEST_CASE("M create attribute with array W ctor {array}",
          "[attribute_values]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array};
  array.Reserve(10);
  array.ArraySetAt(0, DatadogAttribute{30});

  // When
  auto val = array.ArrayGetAt(0);

  // Then
  REQUIRE(val.IntValue() == 30);
}

TEST_CASE("M use same memory in copy W copy {array}", "[attribute_cow]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array};
  array.Reserve(10);
  array.ArraySetAt(0, DatadogAttribute{30});
  DatadogAttribute copy{array};

  // When
  const auto& val = array.ArrayGetAt(0);
  const auto& copy_val = copy.ArrayGetAt(0);

  // Then
  REQUIRE(&val == &copy_val);
}

TEST_CASE("M not overwrite original W SetValue {copy, array}", "[attribute_cow]") {
  // Given
  DatadogAttribute array{DatadogAttribute::Type::Array};
  array.Reserve(10);
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

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

}  // namespace
