// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/primitives/uuid.hpp"

#include <cinttypes>

#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

// "00000000-0000-0000-0000-000000000000": 36 chars + 2 for quotes
static const size_t QUOTED_UUID_LEN = 38;

static void _write_hex_byte(char*& ptr, uint8_t byte) {
  static const char hex_digits[16] = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
  };
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
  *ptr++ = hex_digits[(byte >> 4) & 0xF];
  *ptr++ = hex_digits[byte & 0xF];
  // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
}

/**
 * Returns the exact number of bytes required to encode a UUID value as a JSON string,
 * including quotes.
 */
size_t GetJsonSize(const UUID& value) {
  (void)value;
  return QUOTED_UUID_LEN;
}

/**
 * Encodes the given UUID value as a quoted JSON string literal, lowercase-hex-encoded.
 */
size_t WriteJson(char* dst, size_t n, const UUID& value) {
  DATADOG_ASSERT(n >= QUOTED_UUID_LEN, "insufficient buffer size for UUID");

  // Write opening quote
  char* ptr = dst;
  *ptr++ = '"';

  // Write UUID in standard format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
  _write_hex_byte(ptr, value.bytes[0]);
  _write_hex_byte(ptr, value.bytes[1]);
  _write_hex_byte(ptr, value.bytes[2]);
  _write_hex_byte(ptr, value.bytes[3]);
  *ptr++ = '-';
  _write_hex_byte(ptr, value.bytes[4]);
  _write_hex_byte(ptr, value.bytes[5]);
  *ptr++ = '-';
  _write_hex_byte(ptr, value.bytes[6]);
  _write_hex_byte(ptr, value.bytes[7]);
  *ptr++ = '-';
  _write_hex_byte(ptr, value.bytes[8]);
  _write_hex_byte(ptr, value.bytes[9]);
  *ptr++ = '-';
  _write_hex_byte(ptr, value.bytes[10]);
  _write_hex_byte(ptr, value.bytes[11]);
  _write_hex_byte(ptr, value.bytes[12]);
  _write_hex_byte(ptr, value.bytes[13]);
  _write_hex_byte(ptr, value.bytes[14]);
  _write_hex_byte(ptr, value.bytes[15]);

  // Write closing quote
  *ptr++ = '"';

  const size_t num_bytes_written = ptr - dst;
  DATADOG_ASSERT(
      num_bytes_written == QUOTED_UUID_LEN,
      "unexpected result size for JSON-encoded UUID"
  );
  return num_bytes_written;
}

}  // namespace datadog::impl
