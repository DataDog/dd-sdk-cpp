// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/endian.hpp"

#include <array>
#include <cstdint>

#include "support/catch.hpp"

using namespace datadog::impl;

TEST_CASE("WriteBigEndian16 writes the most significant byte first", "[unit][endian]") {
  // The buffer is larger than the write needs, and the write is in the middle of it, to
  // check that it stays within its expected bounds
  std::array<uint8_t, 4> buf = {0xEE, 0xEE, 0xEE, 0xEE};
  WriteBigEndian16(buf.data() + 1, 0x1234);

  REQUIRE(buf == std::array<uint8_t, 4>{0xEE, 0x12, 0x34, 0xEE});
}

TEST_CASE("WriteBigEndian32 writes the most significant byte first", "[unit][endian]") {
  // As above, the buffer is larger than the write needs
  std::array<uint8_t, 6> buf = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
  WriteBigEndian32(buf.data() + 1, 0x01020304);

  REQUIRE(buf == std::array<uint8_t, 6>{0xEE, 0x01, 0x02, 0x03, 0x04, 0xEE});
}

TEST_CASE("Big-endian routines accept any one-byte buffer type", "[unit][endian]") {
  SECTION("char") {
    std::array<char, 4> buf{};
    WriteBigEndian32(buf.data(), 0xA1B2C3D4);
    REQUIRE(ReadBigEndian32(buf.data()) == 0xA1B2C3D4);
    WriteBigEndian16(buf.data(), 0xA1B2);
    REQUIRE(ReadBigEndian16(buf.data()) == 0xA1B2);
  }

  SECTION("unsigned char") {
    std::array<unsigned char, 4> buf{};
    WriteBigEndian32(buf.data(), 0xA1B2C3D4);
    REQUIRE(ReadBigEndian32(buf.data()) == 0xA1B2C3D4);
    WriteBigEndian16(buf.data(), 0xA1B2);
    REQUIRE(ReadBigEndian16(buf.data()) == 0xA1B2);
  }

  SECTION("uint8_t") {
    std::array<uint8_t, 4> buf{};
    WriteBigEndian32(buf.data(), 0xA1B2C3D4);
    REQUIRE(ReadBigEndian32(buf.data()) == 0xA1B2C3D4);
    WriteBigEndian16(buf.data(), 0xA1B2);
    REQUIRE(ReadBigEndian16(buf.data()) == 0xA1B2);
  }
}

TEST_CASE("ReadBigEndian treats every byte as unsigned", "[unit][endian]") {
  // char may be signed: bytes of 0x80 and above must not be sign-extended into the
  // bytes above them
  const std::array<char, 4> bytes = {
      static_cast<char>(0xFF),
      static_cast<char>(0xFE),
      static_cast<char>(0xFD),
      static_cast<char>(0xFC)
  };
  REQUIRE(ReadBigEndian32(bytes.data()) == 0xFFFEFDFC);
  REQUIRE(ReadBigEndian16(bytes.data()) == 0xFFFE);

  const std::array<char, 2> low = {static_cast<char>(0x80), static_cast<char>(0x01)};
  REQUIRE(ReadBigEndian16(low.data()) == 0x8001);
}

TEST_CASE("Big-endian routines round trip", "[unit][endian]") {
  SECTION("16-bit") {
    const std::array<uint16_t, 7> values = {
        0x0000, 0x0001, 0x00FF, 0x0100, 0x7FFF, 0x8000, 0xFFFF
    };
    for (const uint16_t value : values) {
      std::array<uint8_t, 2> buf{};
      WriteBigEndian16(buf.data(), value);
      REQUIRE(ReadBigEndian16(buf.data()) == value);
    }
  }

  SECTION("32-bit") {
    for (const uint32_t value :
         {0x00000000u,
          0x00000001u,
          0x000000FFu,
          0x00000100u,
          0x0000FFFFu,
          0x00010000u,
          0x00FFFFFFu,
          0x01000000u,
          0x7FFFFFFFu,
          0x80000000u,
          0xFFFFFFFFu}) {
      std::array<uint8_t, 4> buf{};
      WriteBigEndian32(buf.data(), value);
      REQUIRE(ReadBigEndian32(buf.data()) == value);
    }
  }
}
