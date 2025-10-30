// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "events/enum.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>

#include "support/json_serialization.hpp"

using namespace datadog::impl;

enum class FoodType : uint8_t { Apple, Banana, Crab };

DATADOG_STRING_ENUM(
    StringFoodType,
    FoodType,
    DATADOG_ENUM_VALUE(FoodType::Apple, "Apple"),
    DATADOG_ENUM_VALUE(FoodType::Banana, "Banana"),
    DATADOG_ENUM_VALUE(FoodType::Crab, "Crab")
)

TEST_CASE("enum JSON serialization", "[unit][events]") {
  SECTION("M render StringEnum as a JSON string") {
    StringFoodType food_type{FoodType::Apple};
    RequireJsonLiteral(food_type, "\"Apple\"");

    food_type = FoodType::Crab;
    RequireJsonLiteral(food_type, "\"Crab\"");
  }

  SECTION("M render invalid enum as empty string") {
    StringFoodType food_type{static_cast<FoodType>(0xffff)};
    RequireJsonLiteral(food_type, "\"\"");
  }
}
