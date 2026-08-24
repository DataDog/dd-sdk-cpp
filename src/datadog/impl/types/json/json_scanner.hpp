// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace datadog::impl {

/**
 * Private utility used for parsing compact JSON values produced by this SDK. Implements
 * basic JSON decoding logic in order to scan through and identify property values with
 * specific names. Does not handle parsing of literal values, unescaping, extra
 * whitespace, etc.
 *
 * Once Fail() is called, OK() returns false and all subsequent operations are no-ops.
 */
struct JsonScanner {
  std::string_view s;  // Immutable view of input string containing a JSON value
  size_t pos{0};       // Current offset into s; Peek() to read; Advance() to increment

  bool OK() const { return pos != std::string_view::npos; }
  void Fail() { pos = std::string_view::npos; }
  char Peek() const { return pos < s.size() ? s[pos] : '\0'; }
  void Advance(size_t n = 1) { pos += n; }

  /**
   * Result struct indicating the position of a specific value as a substring within the
   * original input string s. The resulting range encompasses the entire JSON literal
   * value: e.g. for a string value, the resulting span will include the enclosing
   * double-quotes and any escape sequences.
   */
  struct Span {
    size_t i{};    // Offset into the original string_view s of value's first byte
    size_t len{};  // Total number of bytes in the value's literal character data

    bool OK() const { return len > 0; }
  };

  /**
   * Skips past a single literal value, returning the offset and length where that value
   * is located in the input string.
   *
   * Triggers failure if the character data at the current offset does not represent a
   * valid JSON value.
   */
  Span SkipValue();

  /**
   * If currently positioned at an instance of the given literal name (e.g. true, false,
   * or null), skips it and returns its range in the input string. Otherwise, triggers
   * failure.
   */
  Span SkipNameLiteral(std::string_view name);

  /**
   * If currently positioned at a literal true or false value, skips it and returns its
   * range in the input string. Otherwise, triggers failure.
   */
  Span SkipBoolLiteral();

  /**
   * If currently positioned at a valid JSON number literal, skips past it and returns
   * the range of the entire value, including any leading minus sign, trailing decimals,
   * etc. Otherwise, triggers failure.
   */
  Span SkipNumberLiteral();

  /**
   * If currently positioned at a valid JSON string literal, skips past it and returns
   * the range of the entire literal value, including quotes and escape sequences.
   * Otherwise, triggers failure.
   */
  Span SkipStringLiteral();

  /**
   * If currently positioned at a valid JSON array literal, skips past it and returns
   * the range of the entire value, including nested values, from the initial '[' to the
   * corresponding ']'. Otherwise, triggers failure.
   */
  Span SkipArrayLiteral();

  /**
   * If currently positioned at a valid JSON object literal, skips past it and returns
   * the range of the entire value, including any and all recursively-nested values,
   * from the initial '{' to the corresponding '}'. Otherwise, triggers failure.
   */
  Span SkipObjectLiteral();

  /**
   * If currently positioned at a valid JSON object literal, advances past the leading
   * '{' and returns true, allowing top-level property values to be parsed via
   * subsequent calls to SkipObjectProperty(), SkipObjectSeparator(), and
   * TrySkipObjectPropertyKey(), for as long as the scanner remains in a state where
   * `OK() && Peek() != '}' between each property.
   *
   * If not positioned at a valid object value, triggers failure and returns false. A
   * return value of false always implies that Fail() has been called.
   */
  bool EnterObject();

  /**
   * Must be called after a property value has been consumed, either via
   * SkipObjectProperty() or by a successful call to TrySkipObjectPropertyKey() plus an
   * explicit value skip.
   *
   * If positioned at a comma, skips past that delimiter so that the next read will
   * occur at the start of the next value. If positioned at a closing curly brace, does
   * nothing. If positioned at any other character, triggers failure.
   */
  void SkipObjectPropertySeparator();

  /**
   * If currently positioned at the beginning of an object property (i.e. the opening
   * quote of a property name, which is followed by a colon and a JSON literal value),
   * advances past both the name and the value for that property, then returns true.
   *
   * If not positioned at a value key-value property, or if either the name or the value
   * can not be parsed as valid JSON, triggers failure and returns false. A return value
   * of false always implies that Fail() has been called.
   */
  bool SkipObjectProperty();

  /**
   * Checks the data at the current position to see whether it is a valid JSON property
   * key with the given name: e.g. `TrySkipObjectPropertyKey("foo")` checks whether the
   * next 6 bytes of string data are exactly `"foo":`.
   *
   * If an exact match is found, the function advances past the key, leaving the current
   * offset at the beginning of the property value, then returns true.
   *
   * If there is no matching property name at the current position, makes no changes to
   * scanner state (does not call Fail(), does not advance the current position) and
   * returns false.
   */
  bool TrySkipObjectPropertyKey(std::string_view name);
};

}  // namespace datadog::impl
