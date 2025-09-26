// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <cinttypes>
#include <optional>
#include <string>
#include <string_view>

#include "datadog/api.hpp"

namespace datadog {

struct UUID {
  std::array<uint8_t, 16> bytes;

  /** Static zero-initialized value. */
  DATADOG_API static const UUID Zero;

  /** Initializes this UUID with a value of zero. */
  DATADOG_API UUID();

  /** Initializes this UUID with the given 16-byte value. */
  DATADOG_API UUID(const uint8_t value[16]);

  // UUID is trivially destructible; it just warps std::array
  ~UUID() = default;

  // Copying and moving are supported w/ generated implementations thanks to std::array
  DATADOG_API UUID(const UUID&);
  DATADOG_API UUID& operator=(const UUID&);
  DATADOG_API UUID(UUID&&) noexcept;
  DATADOG_API UUID& operator=(UUID&&) noexcept;

  /** Copies the provided 16-byte value into this UUID. */
  DATADOG_API UUID& operator=(const uint8_t value[16]);

  /** Constructs a random UUID value using the system implementation. */
  DATADOG_API static UUID Random();

  /** Parses a UUID from a 36-character string representation. */
  DATADOG_API static std::optional<UUID> Parse(std::string_view s);

  /** Encodes this UUID as a 36-character string of lowercase hex digits and hyphens. */
  DATADOG_API std::string ToString() const;

  /**
   * Serializes this value as a 36-byte string, writing directly into the provided
   * buffer, without a null terminator.
   */
  void ToBuffer(char* dst, size_t n) const;

  DATADOG_API bool operator==(const UUID& other) const;
  DATADOG_API bool operator!=(const UUID& other) const;
};

}  // namespace datadog
