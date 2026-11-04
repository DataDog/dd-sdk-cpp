// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>

namespace datadog::impl {

/**
 * Routines for reading and writing multi-byte integers in a fixed byte order,
 * independent of the byte order of the machine, for binary formats that are stored or
 * exchanged. Multi-byte binary values are always written big-endian: most significant
 * byte first.
 *
 * Each routine works on a buffer of any one-byte type (`char`, `unsigned char`,
 * `uint8_t`, ...), so that it can be used on whichever the surrounding code has. Reads
 * treat every byte as unsigned, whether or not `char` is signed.
 */

/**
 * Writes `value` to the two bytes starting at `dst`.
 */
template <typename Byte>
void WriteBigEndian16(Byte* dst, uint16_t value) {
  static_assert(sizeof(Byte) == 1, "WriteBigEndian16 needs a buffer of bytes");
  dst[0] = static_cast<Byte>(value >> 8);
  dst[1] = static_cast<Byte>(value);
}

/**
 * Writes `value` to the four bytes starting at `dst`.
 */
template <typename Byte>
void WriteBigEndian32(Byte* dst, uint32_t value) {
  static_assert(sizeof(Byte) == 1, "WriteBigEndian32 needs a buffer of bytes");
  dst[0] = static_cast<Byte>(value >> 24);
  dst[1] = static_cast<Byte>(value >> 16);
  dst[2] = static_cast<Byte>(value >> 8);
  dst[3] = static_cast<Byte>(value);
}

/**
 * Reads the value in the two bytes starting at `src`.
 */
template <typename Byte>
uint16_t ReadBigEndian16(const Byte* src) {
  static_assert(sizeof(Byte) == 1, "ReadBigEndian16 needs a buffer of bytes");
  const auto b0 = static_cast<uint16_t>(static_cast<uint8_t>(src[0]));
  const auto b1 = static_cast<uint16_t>(static_cast<uint8_t>(src[1]));
  return static_cast<uint16_t>((b0 << 8) | b1);
}

/**
 * Reads the value in the four bytes starting at `src`.
 */
template <typename Byte>
uint32_t ReadBigEndian32(const Byte* src) {
  static_assert(sizeof(Byte) == 1, "ReadBigEndian32 needs a buffer of bytes");
  const auto b0 = static_cast<uint32_t>(static_cast<uint8_t>(src[0]));
  const auto b1 = static_cast<uint32_t>(static_cast<uint8_t>(src[1]));
  const auto b2 = static_cast<uint32_t>(static_cast<uint8_t>(src[2]));
  const auto b3 = static_cast<uint32_t>(static_cast<uint8_t>(src[3]));
  return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

}  // namespace datadog::impl
