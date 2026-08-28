// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/json/json_scanner.hpp"

namespace datadog::impl {

JsonScanner::Span JsonScanner::SkipValue() {
  // Operation is a no-op if parsing has already failed
  if (!OK()) {
    return Span{};
  }

  // Assuming we're positioned at the first byte of a literal value, we can determine
  // its type based on that byte
  const char c = Peek();
  if (c == 'n') {
    return SkipNameLiteral("null");
  }
  if (c == 't') {
    return SkipNameLiteral("true");
  }
  if (c == 'f') {
    return SkipNameLiteral("false");
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    return SkipNumberLiteral();
  }
  if (c == '"') {
    return SkipStringLiteral();
  }
  if (c == '[') {
    return SkipArrayLiteral();
  }
  if (c == '{') {
    return SkipObjectLiteral();
  }

  // If we're not positioned at the first byte of a valid JSON value, abort
  Fail();
  return Span{};
}

JsonScanner::Span JsonScanner::SkipNameLiteral(std::string_view name) {
  // No-op if already failed; if we're not positioned at a value that exactly matches
  // the given name literal, fail
  const size_t n = name.size();
  if (!OK() || pos + n > s.size() || s.substr(pos, n) != name) {
    Fail();
    return Span{};
  }

  // The next n bytes are a match; advance past the value and return its range
  const size_t start = pos;
  Advance(n);
  return Span{start, pos - start};
}

JsonScanner::Span JsonScanner::SkipBoolLiteral() {
  if (Peek() == 't') {
    return SkipNameLiteral("true");
  }
  return SkipNameLiteral("false");
}

JsonScanner::Span JsonScanner::SkipNumberLiteral() {
  // No-op if already failed
  if (!OK()) {
    return Span{};
  }

  // Iterate character-by-character in a few successive states, relying on the fact that
  // Peek() returns '\0' once we've advanced past the end of the string to detect
  // truncation
  const size_t start = pos;

  // A JSON number value may have a leading minus sign; skip past it
  if (Peek() == '-') {
    Advance();
  }

  // Integer part: we need at least one digit (e.g. `.9` and `-.9` are not valid JSON
  // number values)
  if (Peek() < '0' || Peek() > '9') {
    Fail();
    return Span{};
  }

  // A leading zero must stand alone: JSON forbids `007`, `01.5`, etc.
  if (Peek() == '0') {
    Advance();
  } else {
    while (Peek() >= '0' && Peek() <= '9') {
      Advance();
    }
  }

  // Optional fractional part: '.' followed by one or more digits
  if (Peek() == '.') {
    // Skip initial '.'
    Advance();

    // We must have at least one digit after the decimal point: e.g. `1.` is not valid
    // JSON
    if (Peek() < '0' || Peek() > '9') {
      Fail();
      return Span{};
    }

    // Any number of additional digits may follow
    while (Peek() >= '0' && Peek() <= '9') {
      Advance();
    }
  }

  // Optional exponent part: 'e' or 'E', followed by an optional sign, followed by one
  // or more digits
  if (Peek() == 'e' || Peek() == 'E') {
    // Skip past exponent specifier
    Advance();

    // Skip past sign if present
    if (Peek() == '+' || Peek() == '-') {
      Advance();
    }

    // We must have at least one digit: e.g. `1e`, `1.1e+` are not valid JSON
    if (Peek() < '0' || Peek() > '9') {
      Fail();
      return Span{};
    }

    // Any number of additional digits may follow
    while (Peek() >= '0' && Peek() <= '9') {
      Advance();
    }
  }

  // We've advanced past all valid numeric-literal characters without triggering any
  // failure states: our current position is the next character after the number
  return Span{start, pos - start};
}

JsonScanner::Span JsonScanner::SkipStringLiteral() {
  // No-op if already failed; if not at start of a string literal, fail
  if (!OK() || Peek() != '"') {
    Fail();
    return Span{};
  }

  // Record start position and skip opening double-quote
  const size_t start = pos;
  Advance();

  // Iterate byte-byte-byte until we hit a closing (unescaped) double-quote
  while (pos < s.size()) {
    // If the current character is a backslash, it begins an escape sequence, which may
    // be one of multiple lengths (e.g. \\, \", \u0007): as long as we skip the first
    // character of the escape sequence, we'll properly avoid treating escaped
    // double-quotes as the end of the string
    const char c = Peek();
    if (c == '\\') {
      // Skip backslash _and_ the following character, which could be an escaped quote
      Advance(2);
      continue;
    }

    // Otherwise, if we've hit an unescaped double-quote, we're at the end of the value
    if (c == '"') {
      // Advance past closing quote, then return the span from opening quote to closing
      // quote
      Advance();
      return Span{start, pos - start};
    }

    // Regular character: advance past it
    Advance();
  }

  // We found no closing double-quote: string value is unterminated
  Fail();
  return Span{};
}

JsonScanner::Span JsonScanner::SkipArrayLiteral() {
  // No-op if already failed; if not at the opening bracket of an array, fail
  if (!OK() || Peek() != '[') {
    Fail();
    return Span{};
  }

  // Skip past the opening bracket
  const size_t start = pos;
  Advance();

  // Skip one array item (i.e. one complete JSON value) at a time, checking for a
  // closing bracket after each value (not character-by-character)
  while (OK() && Peek() != ']') {
    // Skip the next item
    SkipValue();

    // If the value is followed by a comma, skip it; a trailing comma (i.e. comma
    // immediately before ']') is not valid JSON
    if (Peek() == ',') {
      Advance();
      if (Peek() == ']') {
        Fail();
        return Span{};
      }
    }
  }

  // If OK: we've successfully skipped all array values and we're at the closing ']';
  // otherwise we broke out of the loop due to a SkipValue() failure
  if (!OK()) {
    return Span{};
  }

  // Advance past closing bracket, then return the span from opening bracket to closing
  // bracket
  Advance();
  return Span{start, pos - start};
}

JsonScanner::Span JsonScanner::SkipObjectLiteral() {
  // No-op if already failed; if not at the opening brace of an object value, fail
  const size_t start = pos;
  if (!EnterObject()) {
    return Span{};
  }

  // Continue skipping past <key>:<value> entries until we reach the end of the object
  while (OK() && Peek() != '}') {
    // Skip the next key-value pair
    if (!SkipObjectProperty()) {
      return Span{};
    }

    // If the value is followed by a comma, skip it; a trailing comma (i.e. comma
    // immediately before '}') is not valid JSON
    if (Peek() == ',') {
      Advance();
      if (Peek() == '}') {
        Fail();
        return Span{};
      }
    }
  }

  // If OK: we've successfully skipped all properties and we're at the closing '}';
  // otherwise we broke out of the loop due to a SkipValue() failure
  if (!OK()) {
    return Span{};
  }

  // Advance past closing brace, then return the span from opening brace to closing
  // brace
  Advance();
  return Span{start, pos - start};
}

bool JsonScanner::EnterObject() {
  if (Peek() != '{') {
    Fail();
    return false;
  }
  Advance();
  return true;
}

void JsonScanner::SkipObjectPropertySeparator() {
  if (Peek() == ',') {
    Advance();
  } else if (Peek() != '}') {
    Fail();
  }
}

bool JsonScanner::SkipObjectProperty() {
  // We should be positioned at a string literal representing an object property name
  // (i.e. key): skip past it
  SkipStringLiteral();
  if (!OK()) {
    return false;
  }

  // Next byte should be a colon delimiting the key from the value: skip past it
  if (Peek() != ':') {
    Fail();
    return false;
  }
  Advance();

  // Attempt to parse a literal JSON value from this point; returning true only if we
  // successfully parse something (we don't care what, as long as it was a valid JSON
  // value)
  SkipValue();
  return OK();
}

bool JsonScanner::TrySkipObjectPropertyKey(std::string_view name) {
  // Early-out with no match if already failed
  if (!OK()) {
    return false;
  }

  // We need at least n bytes after the current position: the length of the name, plus
  // enclosing double quotes and a trailing colon
  const size_t name_len = name.size();
  const size_t n = 3 + name_len;

  // Note that we assume `name` contains no characters that require escape sequences
  // to represented in a JSON string literal: this code is only used to parse a
  // well-defined set of property names

  // Check to see if the next n bytes are exactly `"name":`: if not, we're not
  // positioned at this property value and this operation is a no-op
  if (pos + n > s.size() || s[pos] != '"' || s.substr(pos + 1, name_len) != name ||
      s[pos + 1 + name_len] != '"' || s[pos + n - 1] != ':') {
    return false;
  }

  // We've matched on the expected property name: return true and advance to the start
  // of the value so the caller can parse it
  Advance(n);
  return true;
}

}  // namespace datadog::impl
