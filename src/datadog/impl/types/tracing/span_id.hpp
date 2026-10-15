// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string>

namespace datadog::impl {

/**
 * 64-bit Span identifier.
 */
struct SpanID {
  SpanID(uint64_t value);

  bool operator==(const SpanID& other) const { return _value == other._value; };

  bool operator!=(const SpanID& other) const { return !(*this == other); };

  /** Encodes this SpanID as decimal number string. */
  [[nodiscard]] std::string ToDecimalString() const;

  /** Encodes this SpanID as a 16-characters string of lowercase hex digits. */
  [[nodiscard]] std::string ToHexString() const;

  /**
   * True if this is a valid ID, false otherwise.
   *
   * An ID is valid if at least one bit is 1.
   */
  bool IsValid() { return _value != 0; };

 private:
  // The span ID 64 bit value.
  uint64_t _value;
};

}  // namespace datadog::impl
