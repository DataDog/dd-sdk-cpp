// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>

#include "support/json_serialization.hpp"

using namespace datadog::impl;

TEST_CASE("bool JSON serialization", "[unit][json]") {
  SECTION("M render literal true or false") {
    RequireJsonLiteral(true, "true");
    RequireJsonLiteral(false, "false");
  }
}
