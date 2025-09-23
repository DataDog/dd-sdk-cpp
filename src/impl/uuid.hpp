#pragma once

#include <array>
#include <cinttypes>

namespace datadog::impl {

struct uuid {
  std::array<uint8_t, 16> bytes;

  /** Static zero-initialized value. */
  static const uuid zero;

  /** Initializes this UUID with a value of zero. */
  uuid();

  /** Initializes this UUID with the given 16-byte value. */
  uuid(const uint8_t value[16]);

  /** Copies the provided 16-byte value into this UUID. */
  void set(const uint8_t value[16]);

  /** Constructs a random UUID value using the system implementation. */
  static uuid make_random();

  bool operator==(const uuid& other) const;
  bool operator!=(const uuid& other) const;
};

}  // namespace datadog::impl
