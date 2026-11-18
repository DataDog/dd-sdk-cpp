// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/primitives/string.hpp"

#include <cstdint>

#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

/**
 * Returns the exact number of bytes required to represent a string value in JSON,
 * encompassing the surrounding double-quotes and accounting for any characters that
 * must be escaped.
 */
static size_t string_quoted_escaped_len(std::string_view value) {
  // 2 bytes for the surrounding double-quotes, plus N bytes for each character in the
  // string
  size_t len = 2 + value.size();

  // Increase len to account for any characters that will need to be escaped
  for (uint8_t c : value) {
    switch (c) {
      // Double-quotes, backslashes, and control codes with short escape sequences will
      // require an extra byte for the preceding slash that escapes them
      case '\"':
      case '\\':
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
        len += 1;
        break;

      // Other control bytes must be encoded '\u00XX'; all other bytes are emitted
      // unchanged
      default:
        if (c < 0x20) {
          len += 5;
        }
        break;
    }
  }
  return len;
}

static void u00_escape_write(char*& ptr, uint8_t byte) {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
  static const char hex_digits[] = "0123456789abcdef";
  *ptr++ = '\\';
  *ptr++ = 'u';
  *ptr++ = '0';
  *ptr++ = '0';
  *ptr++ = hex_digits[(byte >> 4) & 0xF];
  *ptr++ = hex_digits[byte & 0xF];
  // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
}

static size_t string_quoted_escaped_write(char* dst, size_t n, std::string_view value) {
  char* ptr = dst;

  // Double-quote to open string literal
  *ptr++ = '"';

  // Copy all bytes from the input string, escaping to ensure a valid JSON literal
  for (uint8_t c : value) {
    switch (c) {  // Compare as unsigned to ensure UTF-8 bytes are handled correctly
      // Double-quotes, backslashes, and control codes get a preceding slash
      case '\"':
        *ptr++ = '\\';
        *ptr++ = '\"';
        break;
      case '\\':
        *ptr++ = '\\';
        *ptr++ = '\\';
        break;
      case '\b':
        *ptr++ = '\\';
        *ptr++ = 'b';
        break;
      case '\f':
        *ptr++ = '\\';
        *ptr++ = 'f';
        break;
      case '\n':
        *ptr++ = '\\';
        *ptr++ = 'n';
        break;
      case '\r':
        *ptr++ = '\\';
        *ptr++ = 'r';
        break;
      case '\t':
        *ptr++ = '\\';
        *ptr++ = 't';
        break;

      // Other control bytes must be encoded '\u00XX'; all other bytes are emitted
      // unchanged
      default:
        if (c < 0x20) {
          u00_escape_write(ptr, c);
        } else {
          *ptr++ = static_cast<char>(c);
        }
        break;
    }
  }

  // Double-quote to close string literal
  *ptr++ = '"';

  // Return total number of bytes written, which should have been less than or equal to
  // the available space in the buffer
  const size_t num_bytes_written = ptr - dst;
  DATADOG_ASSERT(num_bytes_written <= n, "buffer overflow on string encode");
  return num_bytes_written;
}

size_t GetJsonSize(const std::string_view& value) {
  return string_quoted_escaped_len(value);
}

size_t WriteJson(char* dst, size_t n, const std::string_view& value) {
  return string_quoted_escaped_write(dst, n, value);
}

}  // namespace datadog::impl
