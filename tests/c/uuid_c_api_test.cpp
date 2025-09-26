// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <cinttypes>
#include <vector>

#include "datadog/uuid.h"

static const uint8_t bytes_ccb7[16] = {
    204, 183, 144, 132, 188, 43, 69, 73, 187, 199, 242, 126, 21, 63, 212, 182
};

static const uint8_t bytes_99a7[16] = {
    153, 167, 196, 130, 39, 33, 71, 143, 171, 140, 191, 244, 161, 252, 181, 117
};

TEST_CASE("dd_uuid", "[unit][attribute][c-api]") {
  SECTION("M zero-initialize W dd_uuid_init is called") {
    dd_uuid_t value;
    dd_uuid_init(&value);
    for (size_t i = 0; i < sizeof(value.bytes); i++) {
      REQUIRE(value.bytes[i] == 0);
    }
  }

  SECTION("M initialize to a random value W dd_uuid_random called") {
    dd_uuid_t value;
    dd_uuid_init(&value);
    dd_uuid_random(&value);
    REQUIRE(dd_uuid_is_zero(&value) == false);

    dd_uuid_t other;
    dd_uuid_random(&other);
    REQUIRE(dd_uuid_is_zero(&other) == false);
    REQUIRE(std::memcmp(value.bytes, other.bytes, 16) != 0);
  }

  SECTION("M copy provided bytes W dd_uuid_set is called") {
    dd_uuid_t value;
    dd_uuid_set(&value, bytes_ccb7);
    REQUIRE(std::memcmp(value.bytes, bytes_ccb7, sizeof(value.bytes)) == 0);
  }

  SECTION("M initialize bytes W dd_uuid_parse called with valid string") {
    std::vector<const char*> strings = {
        "ccb79084-bc2b-4549-bbc7-f27e153fd4b6",
        "CCB79084-BC2B-4549-BBC7-F27E153FD4B6",
        "cCB79084-bC2B-4549-bbC7-f27E153fd4B6"
    };
    for (const char* s : strings) {
      dd_uuid_t value;
      const bool ok = dd_uuid_parse(&value, s);
      REQUIRE(ok);
      REQUIRE(std::memcmp(value.bytes, bytes_ccb7, sizeof(value.bytes)) == 0);
    }
  }

  SECTION("M reject value W dd_uuid_parse called with invalid UUID strings") {
    std::vector<const char*> strings = {
        "",
        "not-a-valid-uuid",
        "ccb79084-bad1-hex1-bbc7-f27e153fd4b6"
        "ccb79084-bc2b-4549-bbc7-f27e153fd4b6-",
        "-cb79084-bc2b-4549-bbc7-f27e153fd4b6",
    };
    for (const char* s : strings) {
      dd_uuid_t value;
      const bool ok = dd_uuid_parse(&value, s);
      REQUIRE(!ok);
    }
  }

  SECTION("M format UUID correctly W dd_uuid_to_string called") {
    dd_uuid_t value;
    char buf[37];

    dd_uuid_set(&value, bytes_ccb7);
    dd_uuid_to_string(&value, buf);
    REQUIRE(std::strcmp(buf, "ccb79084-bc2b-4549-bbc7-f27e153fd4b6") == 0);

    dd_uuid_set(&value, bytes_99a7);
    dd_uuid_to_string(&value, buf);
    REQUIRE(std::strcmp(buf, "99a7c482-2721-478f-ab8c-bff4a1fcb575") == 0);
  }

  SECTION("M detect zero W dd_uuid_is_zero called") {
    dd_uuid_t value;
    dd_uuid_init(&value);
    REQUIRE(dd_uuid_is_zero(&value) == true);
    value.bytes[0] = 42;
    REQUIRE(dd_uuid_is_zero(&value) == false);
  }

  SECTION("M safely do nothing W called with null arguments") {
    dd_uuid_t value;
    char buf[37];

    dd_uuid_init(nullptr);
    dd_uuid_random(nullptr);
    dd_uuid_set(nullptr, bytes_99a7);
    dd_uuid_set(&value, nullptr);
    REQUIRE(dd_uuid_parse(nullptr, "ccb79084-bc2b-4549-bbc7-f27e153fd4b6") == false);
    REQUIRE(dd_uuid_parse(&value, nullptr) == false);
    dd_uuid_to_string(nullptr, buf);
    dd_uuid_to_string(&value, nullptr);
    REQUIRE(dd_uuid_is_zero(nullptr) == false);
  }
}
