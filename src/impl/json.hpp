// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <vector>

#include "assert.hpp"
#include "datadog/attribute.hpp"

// clang-format off
#include "json/primitives/bool.hpp"
#include "json/primitives/float.hpp"
#include "json/primitives/integer.hpp"
#include "json/primitives/null.hpp"
#include "json/primitives/string.hpp"
#include "json/primitives/timestamp.hpp"
#include "json/primitives/uuid.hpp"
#include "json/optional.hpp"
// clang-format on

namespace datadog::impl {

/**
 * This file is the top-level header for the Datadog SDK's JSON serialization support.
 * To ensure consistent definition of function overloads etc., any TU that needs to
 * encode values as JSON should include this header and no other `json/` header.
 *
 * To encode a value of any JSON-serializable type, use `EncodeJson`.
 *
 * The files within in `src/impl/json/` define JSON serialization functions for all
 * supported types. For each type, those definitions consists of two functions:
 *
 * size_t datadog::impl::GetJsonSize(const T& value):
 *   Given a value of type T, returns the number of bytes required to store the
 *   JSON-encoded string representation of that value. In many cases, the result is an
 *   exact-fit size, but the worst-case maximum size may be returned in cases where it's
 *   impractical to compute a precise size (e.g. floats).
 *
 * size_t datadog::impl::WriteJson(char* dst, size_t n, const T& value):
 *   Given a value of type T and an output buffer `dst` with `n` bytes available, writes
 *   the given value to the buffer as a JSON-encoded string. Returns the number of bytes
 *   written. Does not perform bounds checking, except for fatal assertions when
 *   WITH_DATADOG_ASSERTS is enabled: required buffer size should always be precomputed
 *   via GetJsonSize().
 */

/**
 * Given a value of type T, serializes it to the given buffer in JSON format.
 */
template <typename T>
void EncodeJson(std::vector<uint8_t>& out_buffer, const T& value) {
  // Ensure that our buffer has space to fit this value when JSON-serialized
  const size_t precomputed_size = GetJsonSize(value);
  out_buffer.resize(precomputed_size);

  // Serialize our value into the buffer
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  char* dst = reinterpret_cast<char*>(out_buffer.data());
  const size_t num_bytes_written = WriteJson(dst, precomputed_size, value);

  // Debug: verify that WriteValue never writes more data than ComputeValueLen indicates
  // we need
  DATADOG_ASSERT(
      num_bytes_written <= precomputed_size, "unexpected overflow of JSON buffer"
  );

  // Ensure that out_buffer is bounded to include only the data we've written, in case
  // we overestimated buffer size
  out_buffer.resize(num_bytes_written);
}

}  // namespace datadog::impl
