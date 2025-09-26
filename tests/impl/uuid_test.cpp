// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "uuid.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace datadog::impl;

TEST_CASE("uuid", "[unit][uuid]") {
  SECTION("M default-initialize to zero") {
    // When we default-initialize a uuid struct
    uuid value;

    // Then it holds zero
    for (size_t i = 0; i < 16; i++) {
      REQUIRE(value.bytes[i] == 0);
    }
  }

  SECTION("M initialize to zero W assigned from uuid::zero") {
    // When we initialize a uuid struct from a uuid::zero
    uuid value = uuid::zero;

    // Then it holds zero
    for (size_t i = 0; i < 16; i++) {
      REQUIRE(value.bytes[i] == 0);
    }
  }

  SECTION("M equal zero W default-initialized") {
    uuid value;
    REQUIRE(value == uuid::zero);
  }

  SECTION("M initialize from raw byte array") {
    // Given the bytes for the UUID value fc54045f-6cb5-4896-a11c-f553546e5e18
    const uint8_t bytes[16] = {
        252, 84, 4, 95, 108, 181, 72, 150, 161, 28, 245, 83, 84, 110, 94, 24
    };

    // When we initialize a uuid value from those bytes
    uuid value(bytes);

    // Then those values are held by the uuid struct in the expected order
    REQUIRE(value.bytes[0] == 252);
    REQUIRE(value.bytes[1] == 84);
    REQUIRE(value.bytes[14] == 94);
    REQUIRE(value.bytes[15] == 24);
  }

  SECTION("M support set and comparison") {
    // Given fc54045f-6cb5-4896-a11c-f553546e5e18 and
    // 3975cb39-25dd-426b-aa44-219536503b06
    const uint8_t bytes_fc54[16] = {
        252, 84, 4, 95, 108, 181, 72, 150, 161, 28, 245, 83, 84, 110, 94, 24
    };
    const uint8_t bytes_3975[16] = {
        57, 117, 203, 57, 37, 221, 66, 107, 170, 68, 33, 149, 54, 80, 59, 6
    };

    // When we initialize two uuid values from those byte patterns
    uuid value_a(bytes_fc54);
    uuid value_b(bytes_3975);

    // Then those values are distinct
    REQUIRE(value_a != value_b);

    // Next: When we set value_a equal to value_b
    value_a = value_b;

    // Then the values are equal
    REQUIRE(value_a == value_b);

    // Next: Wehn we set value_a equal to uuid::zero
    value_a = uuid::zero;

    // Then the values are distinct again
    REQUIRE(value_a == uuid::zero);
    REQUIRE(value_a != value_b);

    // Next: When we set both values from the original byte pattern fc54
    value_a.set(bytes_fc54);
    value_b.set(bytes_fc54);

    // Then they're equal
    REQUIRE(value_a == value_b);
    REQUIRE(value_a != uuid::zero);
    REQUIRE(value_b != uuid::zero);
  }

  SECTION("M generate distinct values W constructed via make_random") {
    // When we generate two random UUIDs
    const uuid value_a = uuid::make_random();
    const uuid value_b = uuid::make_random();

    // Then they hold different values
    REQUIRE(value_a != value_b);
    REQUIRE(value_a != uuid::zero);
    REQUIRE(value_b != uuid::zero);
  }
}
