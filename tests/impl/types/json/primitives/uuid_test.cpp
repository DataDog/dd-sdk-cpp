// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/uuid.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("uuid JSON serialization", "[unit][json]") {
  SECTION("M render 36-char, lowercase-hex-encoded UUID as JSON string") {
    RequireJsonLiteral(UUID::Zero, "\"00000000-0000-0000-0000-000000000000\"");
    RequireJsonLiteral(
        *UUID::Parse("d137ea4b-9981-4f9e-a588-68c843bb189c"),
        "\"d137ea4b-9981-4f9e-a588-68c843bb189c\""
    );
  }

  SECTION("M faithfully preserve random UUID values") {
    // For good measure, just generate 100 random UUIDs and verify that our JSON
    // representation matches what UUID::ToString() tells us we should get
    for (int i = 0; i < 100; i++) {
      UUID value = UUID::Random();
      std::string want = '"' + value.ToString() + '"';
      RequireJsonLiteral(value, want);
    }
  }
}
