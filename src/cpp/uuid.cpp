// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/uuid.hpp"

#include <cinttypes>
#include <cstring>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#pragma comment(lib, "ole32.lib")
#else
#include <uuid/uuid.h>
#endif

#include "assert.hpp"

namespace datadog {

const UUID UUID::Zero;

UUID::UUID() : bytes{} {}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
UUID::UUID(const uint8_t value[16]) { std::memcpy(bytes.data(), value, 16); }

UUID::UUID(const UUID&) noexcept = default;
UUID& UUID::operator=(const UUID&) noexcept = default;
UUID::UUID(UUID&&) noexcept = default;
UUID& UUID::operator=(UUID&&) noexcept = default;

/** Copies the provided 16-byte value into this UUID. */
UUID& UUID::operator=(const uint8_t value[16]) {
  std::memcpy(bytes.data(), value, 16);
  return *this;
}

UUID UUID::Random() {
  UUID value{};
#ifdef _WIN32
  GUID guid;
  HRESULT hr = ::CoCreateGuid(&guid);
  DATADOG_ASSERT(SUCCEEDED(hr), "CoCreateGuid failed");
  value.bytes[0] = static_cast<uint8_t>((guid.Data1 >> 24) & 0xFF);
  value.bytes[1] = static_cast<uint8_t>((guid.Data1 >> 16) & 0xFF);
  value.bytes[2] = static_cast<uint8_t>((guid.Data1 >> 8) & 0xFF);
  value.bytes[3] = static_cast<uint8_t>((guid.Data1) & 0xFF);
  value.bytes[4] = static_cast<uint8_t>((guid.Data2 >> 8) & 0xFF);
  value.bytes[5] = static_cast<uint8_t>((guid.Data2) & 0xFF);
  value.bytes[6] = static_cast<uint8_t>((guid.Data3 >> 8) & 0xFF);
  value.bytes[7] = static_cast<uint8_t>((guid.Data3) & 0xFF);
  value.bytes[8] = guid.Data4[0];
  value.bytes[9] = guid.Data4[1];
  value.bytes[10] = guid.Data4[2];
  value.bytes[11] = guid.Data4[3];
  value.bytes[12] = guid.Data4[4];
  value.bytes[13] = guid.Data4[5];
  value.bytes[14] = guid.Data4[6];
  value.bytes[15] = guid.Data4[7];
#else
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  uuid_generate(value.bytes.data());
#endif
  return value;
}

std::optional<UUID> UUID::Parse(std::string_view s) {
  // Validate UUID format
  if (s.size() != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
    return std::nullopt;
  }
  for (int i = 0; i < 36; i++) {
    const char c = s[i];
    const bool should_be_hyphen = i == 8 || i == 13 || i == 18 || i == 23;
    if (should_be_hyphen) {
      if (c != '-') {
        return std::nullopt;
      }
    } else {
      const bool is_hex =
          (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
      if (!is_hex) {
        return std::nullopt;
      }
    }
  }

  // Now that we're sure this is a valid UUID string (with either uppercase or
  // lowercase hex digits), we can parse without branching for bounds/value checks
  auto parse_hex_nibble = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    DATADOG_ASSERT(c >= 'A' && c <= 'F', "invalid UUID");
    return c - 'A' + 10;
  };
  auto parse_hex_byte = [parse_hex_nibble](char hi, char lo) -> uint8_t {
    return (parse_hex_nibble(hi) << 4) | parse_hex_nibble(lo);
  };
  const uint8_t bytes[16] = {
      parse_hex_byte(s[0], s[1]),
      parse_hex_byte(s[2], s[3]),
      parse_hex_byte(s[4], s[5]),
      parse_hex_byte(s[6], s[7]),
      parse_hex_byte(s[9], s[10]),
      parse_hex_byte(s[11], s[12]),
      parse_hex_byte(s[14], s[15]),
      parse_hex_byte(s[16], s[17]),
      parse_hex_byte(s[19], s[20]),
      parse_hex_byte(s[21], s[22]),
      parse_hex_byte(s[24], s[25]),
      parse_hex_byte(s[26], s[27]),
      parse_hex_byte(s[28], s[29]),
      parse_hex_byte(s[30], s[31]),
      parse_hex_byte(s[32], s[33]),
      parse_hex_byte(s[34], s[35])
  };
  return UUID(static_cast<const uint8_t*>(bytes));
}

std::string UUID::ToString() const {
  // Encode the raw bytes of the string-encoded UUID, w/o null terminator
  char buf[36];
  ToBytes(static_cast<char*>(buf), sizeof(buf));

  // Construct a new std::string, specifying exact length
  return std::string(static_cast<const char*>(buf), sizeof(buf));
}

void UUID::ToBytes(char* dst, size_t n) const {
  // Require at least 36 bytes (we don't write a null terminator)
  if (n < 36) {
    DATADOG_ASSERT(false, "buffer passed to UUID::ToBytes is too small");
    return;
  }

  // Encode as hex, byte-by-byte, with delimiters
  auto write_hex_byte = [](uint8_t b, char* s) {
    static const char hex_digits[16] = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    *s = hex_digits[(b >> 4) & 0xf];
    *(s + 1) = hex_digits[b & 0xf];
  };
  write_hex_byte(bytes[0], &dst[0]);
  write_hex_byte(bytes[1], &dst[2]);
  write_hex_byte(bytes[2], &dst[4]);
  write_hex_byte(bytes[3], &dst[6]);
  dst[8] = '-';
  write_hex_byte(bytes[4], &dst[9]);
  write_hex_byte(bytes[5], &dst[11]);
  dst[13] = '-';
  write_hex_byte(bytes[6], &dst[14]);
  write_hex_byte(bytes[7], &dst[16]);
  dst[18] = '-';
  write_hex_byte(bytes[8], &dst[19]);
  write_hex_byte(bytes[9], &dst[21]);
  dst[23] = '-';
  write_hex_byte(bytes[10], &dst[24]);
  write_hex_byte(bytes[11], &dst[26]);
  write_hex_byte(bytes[12], &dst[28]);
  write_hex_byte(bytes[13], &dst[30]);
  write_hex_byte(bytes[14], &dst[32]);
  write_hex_byte(bytes[15], &dst[34]);
}

bool UUID::operator==(const UUID& other) const { return bytes == other.bytes; }

bool UUID::operator!=(const UUID& other) const { return bytes != other.bytes; }

}  // namespace datadog
