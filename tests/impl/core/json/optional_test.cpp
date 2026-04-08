// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "support/json_serialization.hpp"

using namespace datadog::impl;

TEST_CASE("optional JSON serialization", "[unit][json]") {
  SECTION("M render null W no value is set") {
    RequireJsonLiteral(std::optional<int>{}, "null");
    RequireJsonLiteral(std::optional<std::string_view>{}, "null");
  }

  SECTION("M render value type W value is set") {
    RequireJsonLiteral(std::optional<int>{42}, "42");
    RequireJsonLiteral(std::optional<std::string>{"hello"}, "\"hello\"");
  }
}
