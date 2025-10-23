// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <string_view>
#include <vector>

#include "datadog/uuid.hpp"

static const uint8_t bytes_ccb7[16] = {
    204, 183, 144, 132, 188, 43, 69, 73, 187, 199, 242, 126, 21, 63, 212, 182
};

using namespace datadog;

TEST_CASE("UUID", "[unit][uuid][cpp-api]") {
  SECTION("M default-initialize to Zero") {
    // When we default-initialize a uuid struct
    UUID value;

    // Then it holds Zero
    for (size_t i = 0; i < 16; i++) {
      REQUIRE(value.bytes[i] == 0);
    }
  }

  SECTION("M initialize to Zero W assigned from UUID::Zero") {
    // When we initialize a uuid struct from a UUID::Zero
    UUID value = UUID::Zero;

    // Then it holds Zero
    for (size_t i = 0; i < 16; i++) {
      REQUIRE(value.bytes[i] == 0);
    }
  }

  SECTION("M equal Zero W default-initialized") {
    UUID value;
    REQUIRE(value == UUID::Zero);
  }

  SECTION("M generate distinct values W constructed via UUID::Random") {
    // When we generate two random UUIDs
    const UUID value_a = UUID::Random();
    const UUID value_b = UUID::Random();

    // Then they hold different values
    REQUIRE(value_a != value_b);
    REQUIRE(value_a != UUID::Zero);
    REQUIRE(value_b != UUID::Zero);
  }

  SECTION("M initialize from raw byte array") {
    // Given the bytes for the UUID value fc54045f-6cb5-4896-a11c-f553546e5e18
    const uint8_t bytes[16] = {
        252, 84, 4, 95, 108, 181, 72, 150, 161, 28, 245, 83, 84, 110, 94, 24
    };

    // When we initialize a uuid value from those bytes
    UUID value(bytes);

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
    UUID value_a(bytes_fc54);
    UUID value_b(bytes_3975);

    // Then those values are distinct
    REQUIRE(value_a != value_b);

    // Next: When we set value_a equal to value_b
    value_a = value_b;

    // Then the values are equal
    REQUIRE(value_a == value_b);

    // Next: When we set value_a equal to UUID::Zero
    value_a = UUID::Zero;

    // Then the values are distinct again
    REQUIRE(value_a == UUID::Zero);
    REQUIRE(value_a != value_b);

    // Next: When we set both values from the original byte pattern fc54
    value_a = bytes_fc54;
    value_b = bytes_fc54;

    // Then they're equal
    REQUIRE(value_a == value_b);
    REQUIRE(value_a != UUID::Zero);
    REQUIRE(value_b != UUID::Zero);
  }

  SECTION("M initialize bytes W UUID::Parse called with valid string") {
    std::vector<std::string_view> strings = {
        "ccb79084-bc2b-4549-bbc7-f27e153fd4b6",
        "CCB79084-BC2B-4549-BBC7-F27E153FD4B6",
        "cCB79084-bC2B-4549-bbC7-f27E153fd4B6"
    };
    for (std::string_view s : strings) {
      const auto got = UUID::Parse(s);
      REQUIRE(got.has_value());
      REQUIRE(std::memcmp(got->bytes.data(), bytes_ccb7, 16) == 0);
    }
  }

  SECTION("M return nullopt W UUID::Parse called with invalid string") {
    std::vector<std::string_view> strings = {
        "",
        "not-a-valid-uuid",
        "ccb79084-bad1-hex1-bbc7-f27e153fd4b6"
        "ccb79084-bc2b-4549-bbc7-f27e153fd4b6-",
        "-cb79084-bc2b-4549-bbc7-f27e153fd4b6",
    };
    for (std::string_view s : strings) {
      const auto got = UUID::Parse(s);
      REQUIRE(!got.has_value());
    }
  }
}
