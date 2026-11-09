// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/platform/uuid.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <set>

using namespace datadog;

// [platform-uuid] tests validate the default UUID implementation for this platform.

using UuidBytes = std::array<uint8_t, 16>;

static UuidBytes generate() {
  UuidBytes bytes{};
  platform::UuidGenerate(bytes.data());
  return bytes;
}

TEST_CASE("UuidGenerate", "[unit][platform-uuid]") {
  SECTION("M set version 4 and RFC 9562 variant bits W called") {
    for (int i = 0; i < 100; i++) {
      // When we generate a UUID
      const UuidBytes bytes = generate();

      // Then the high nibble of byte 6 is the version: 0100
      REQUIRE((bytes[6] & 0xF0) == 0x40);

      // And the top two bits of byte 8 are the variant: 10
      REQUIRE((bytes[8] & 0xC0) == 0x80);
    }
  }

  SECTION("M return distinct values W called repeatedly") {
    // When we generate many UUIDs
    const int count = 1000;
    std::set<UuidBytes> seen;
    for (int i = 0; i < count; i++) {
      seen.insert(generate());
    }

    // Then every one is distinct
    REQUIRE(seen.size() == static_cast<size_t>(count));
  }
}
