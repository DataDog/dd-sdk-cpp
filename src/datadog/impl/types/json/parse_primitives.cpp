// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/json/parse_primitives.hpp"

#include <charconv>
#include <cstdint>

namespace datadog::impl {

bool ParseJsonBool(std::string_view json_literal, bool& out) {
  if (json_literal == "true") {
    out = true;
    return true;
  }
  if (json_literal == "false") {
    out = false;
    return true;
  }
  return false;
}

bool ParseJsonInt64(std::string_view json_literal, int64_t& out) {
  // Use <charconv>, checking that `ptr` ends up at the end of the literal to ensure
  // that we correctly reject non-integer values
  const char* begin = json_literal.data();
  const char* end = begin + json_literal.size();
  auto [ptr, ec] = std::from_chars(begin, end, out);
  return ec == std::errc{} && ptr == end;
}

bool ParseJsonUInt64(std::string_view json_literal, uint64_t& out) {
  const char* begin = json_literal.data();
  const char* end = begin + json_literal.size();
  auto [ptr, ec] = std::from_chars(begin, end, out);
  return ec == std::errc{} && ptr == end;
}

bool ParseJsonDouble(std::string_view json_literal, double& out) {
  // Use <charconv>, matching the same 'general' format used by our encoding routine
  const char* begin = json_literal.data();
  const char* end = begin + json_literal.size();
  auto [ptr, ec] = std::from_chars(begin, end, out, std::chars_format::general);
  return ec == std::errc{} && ptr == end;
}

bool ParseJsonUUID(std::string_view json_literal, UUID& out) {
  // A UUID should be encoded as a JSON string exactly 36 bytes in length
  if (json_literal.size() != 38 || json_literal[0] != '"' || json_literal[37] != '"') {
    return false;
  }

  // Use UUID::Parse to decode the value enclosed by quotes, returning success
  auto uuid_opt = UUID::Parse(json_literal.substr(1, 36));
  if (!uuid_opt.has_value()) {
    return false;
  }
  out = *uuid_opt;
  return true;
}

bool ParseJsonString(std::string_view json_literal, std::string& out) {
  // A string literal value should begin and end with double quotes; take `inner` as the
  // substring enclosed by those quotes
  if (json_literal.size() < 2 || json_literal.front() != '"' ||
      json_literal.back() != '"') {
    return false;
  }
  const std::string_view inner = json_literal.substr(1, json_literal.size() - 2);

  // In the typical case, our output string will need to hold `inner` exactly; if there
  // are any escape sequences, the final size will be slightly less than that
  out.clear();
  out.reserve(inner.size());

  // Begin iterating over `inner` using index `i`, which we'll advance
  // character-by-character, skipping over escape sequences as we decode them
  size_t i = 0;

  // Helper: given a char in [0..f], return an integer in [0..15], or -1 if invalid hex
  // clang-format off
  auto hex_nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
  };
  auto consume_escape_sequence = [&i, &inner, &hex_nibble](std::string& o) -> bool {
    // `i` is currently positioned at a backslash: we need at least one more byte
    if (i + 1 >= inner.size()) {
      return false;
    }
    ++i;
    switch (inner[i]) {
      case '"':  o += '"';  ++i; return true;
      case '\\': o += '\\'; ++i; return true;
      case 'b':  o += '\b'; ++i; return true;
      case 'f':  o += '\f'; ++i; return true;
      case 'n':  o += '\n'; ++i; return true;
      case 'r':  o += '\r'; ++i; return true;
      case 't':  o += '\t'; ++i; return true;
      case '/':  o += '/';  ++i; return true;
      case 'u': {
        // Only \u00XX (two leading zero hex digits) is supported
        if (i + 4 >= inner.size() || inner[i + 1] != '0' || inner[i + 2] != '0') {
          return false;
        }
        const int hi = hex_nibble(inner[i + 3]);
        const int lo = hex_nibble(inner[i + 4]);
        if (hi < 0 || lo < 0) {
          return false;
        }
        o += static_cast<char>((hi << 4) | lo);
        i += 5;
        return true;
      }
      default: return false;
    }
  };
  // clang-format on

  while (i < inner.size()) {
    const char c = inner[i];
    if (c == '\\') {
      if (!consume_escape_sequence(out)) {
        return false;
      }
    } else {
      out += c;
      ++i;
    }
  }
  return true;
}

}  // namespace datadog::impl
