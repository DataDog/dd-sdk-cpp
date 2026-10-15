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
 * 128-bit Trace identifier.
 */
struct TraceID {
  TraceID(uint64_t high, uint64_t low);

  bool operator==(const TraceID& other) const {
    return _high == other._high && _low == other._low;
  };

  bool operator!=(const TraceID& other) const { return !(*this == other); };

  /** Encodes this TraceID's least significant 64 bits as decimal string. */
  [[nodiscard]] std::string ToLowDecimalString() const;

  /** Encodes this TraceID as a 32-characters string of lowercase hex digits. */
  [[nodiscard]] std::string ToHexString() const;

  /**
   * True if this is a valid ID, false otherwise.
   *
   * An ID is valid if at least one bit is 1.
   */
  bool IsValid() { return _high != 0 || _low != 0; };

 private:
  // The most significant 64 bits.
  uint64_t _high;

  // The least significant 64 bits.
  uint64_t _low;
};

}  // namespace datadog::impl
