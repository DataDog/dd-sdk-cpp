// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "support/json_serialization.hpp"

using namespace datadog::impl;

TEST_CASE("null JSON serialization", "[unit][json]") {
  SECTION("M render literal null") {
    // A literal nullptr_t value will be serialized as a JSON null
    RequireJsonLiteral(nullptr, "null");

    // Note that C-style NULL is just 0 and will be serialized as a number
    RequireJsonLiteral(NULL, "0");
  }
}
