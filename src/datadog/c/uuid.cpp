// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/uuid.h"

#include <cstring>

#include "datadog/uuid.hpp"

#include "datadog/impl/types/assert.hpp"

extern "C" {

void dd_uuid_init(dd_uuid_t* value) {
  if (!value) {
    return;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  std::memset(value->bytes, 0, sizeof(value->bytes));
}

void dd_uuid_random(dd_uuid_t* value) {
  if (!value) {
    return;
  }
  datadog::UUID cpp_value = datadog::UUID::Random();
  dd_uuid_set(value, cpp_value.bytes.data());
}

void dd_uuid_set(dd_uuid_t* value, const uint8_t bytes[16]) {
  if (!value || !bytes) {
    return;
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  std::memcpy(value->bytes, bytes, sizeof(value->bytes));
}

bool dd_uuid_parse(dd_uuid_t* value, const char* s) {
  // Require valid arguments
  if (!value || !s) {
    return false;
  }

  // Validate UUID format
  const size_t len = std::strlen(s);
  if (len != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
    return false;
  }
  for (int i = 0; i < 36; i++) {
    const char c = s[i];
    const bool should_be_hyphen = i == 8 || i == 13 || i == 18 || i == 23;
    if (should_be_hyphen) {
      if (c != '-') {
        return false;
      }
    } else {
      const bool is_hex =
          (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
      if (!is_hex) {
        return false;
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
  value->bytes[0] = parse_hex_byte(s[0], s[1]);
  value->bytes[1] = parse_hex_byte(s[2], s[3]);
  value->bytes[2] = parse_hex_byte(s[4], s[5]);
  value->bytes[3] = parse_hex_byte(s[6], s[7]);
  value->bytes[4] = parse_hex_byte(s[9], s[10]);
  value->bytes[5] = parse_hex_byte(s[11], s[12]);
  value->bytes[6] = parse_hex_byte(s[14], s[15]);
  value->bytes[7] = parse_hex_byte(s[16], s[17]);
  value->bytes[8] = parse_hex_byte(s[19], s[20]);
  value->bytes[9] = parse_hex_byte(s[21], s[22]);
  value->bytes[10] = parse_hex_byte(s[24], s[25]);
  value->bytes[11] = parse_hex_byte(s[26], s[27]);
  value->bytes[12] = parse_hex_byte(s[28], s[29]);
  value->bytes[13] = parse_hex_byte(s[30], s[31]);
  value->bytes[14] = parse_hex_byte(s[32], s[33]);
  value->bytes[15] = parse_hex_byte(s[34], s[35]);
  return true;
}

void dd_uuid_to_string(const dd_uuid_t* value, char out_s[37]) {
  if (!value || !out_s) {
    return;
  }
  auto write_hex_byte = [](uint8_t b, char* s) {
    static const char hex_digits[16] = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };
    *s = hex_digits[(b >> 4) & 0xf];
    *(s + 1) = hex_digits[b & 0xf];
  };
  write_hex_byte(value->bytes[0], out_s + 0);
  write_hex_byte(value->bytes[1], out_s + 2);
  write_hex_byte(value->bytes[2], out_s + 4);
  write_hex_byte(value->bytes[3], out_s + 6);
  out_s[8] = '-';
  write_hex_byte(value->bytes[4], out_s + 9);
  write_hex_byte(value->bytes[5], out_s + 11);
  out_s[13] = '-';
  write_hex_byte(value->bytes[6], out_s + 14);
  write_hex_byte(value->bytes[7], out_s + 16);
  out_s[18] = '-';
  write_hex_byte(value->bytes[8], out_s + 19);
  write_hex_byte(value->bytes[9], out_s + 21);
  out_s[23] = '-';
  write_hex_byte(value->bytes[10], out_s + 24);
  write_hex_byte(value->bytes[11], out_s + 26);
  write_hex_byte(value->bytes[12], out_s + 28);
  write_hex_byte(value->bytes[13], out_s + 30);
  write_hex_byte(value->bytes[14], out_s + 32);
  write_hex_byte(value->bytes[15], out_s + 34);
  out_s[36] = '\0';
}

bool dd_uuid_is_zero(const dd_uuid_t* value) {
  if (!value) {
    return false;
  }
  for (size_t i = 0; i < sizeof(value->bytes); i++) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    if (value->bytes[i] != 0) {
      return false;
    }
  }
  return true;
}
}
