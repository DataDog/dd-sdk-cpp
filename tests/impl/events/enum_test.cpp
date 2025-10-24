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

constexpr std::string_view FoodTypeNames[] = {"Apple", "Banana", "Crab"};
static_assert(FoodTypeNames[static_cast<size_t>(FoodType::Apple)] == "Apple", "");
static_assert(FoodTypeNames[static_cast<size_t>(FoodType::Banana)] == "Banana", "");
static_assert(FoodTypeNames[static_cast<size_t>(FoodType::Crab)] == "Crab", "");

using FoodTypeEnum = StringEnum<FoodType, FoodTypeNames, std::size(FoodTypeNames)>;

TEST_CASE("enum JSON serialization", "[unit][events]") {
  SECTION("M render StringEnum as a JSON string") {
    FoodTypeEnum food_type{FoodType::Apple};
    RequireJsonValue(food_type, "\"Apple\"");

    food_type = FoodType::Crab;
    RequireJsonValue(food_type, "\"Crab\"");
  }

  SECTION("M render invalid enum as empty string") {
    FoodTypeEnum food_type{static_cast<FoodType>(0xffff)};
    RequireJsonValue(food_type, "\"\"");
  }
}
