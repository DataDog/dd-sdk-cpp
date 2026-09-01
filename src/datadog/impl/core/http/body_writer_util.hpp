// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

/**
 * Utility function for use in HttpBodyWriter implementations.
 */
inline size_t _chunked_write(
    char* dst, size_t dst_size, const char* src, size_t src_size
) {
  // Checks for zero size should occur before we're called
  DATADOG_ASSERT(dst_size > 0, "Attempted write with zero-length dst buffer");
  DATADOG_ASSERT(src_size > 0, "Attempted write with zero-length src buffer");

  // Copy from src as many bytes as will fit in dst
  const size_t num_bytes_to_copy = std::min(dst_size, src_size);
  std::memcpy(dst, src, num_bytes_to_copy);
  return num_bytes_to_copy;
}

inline size_t _handle_chunked_write(
    char* dst,
    size_t dst_size,
    const char* src_buffer,
    size_t src_buffer_size,
    size_t& mut_offset
) {
  // Compute the position we're reading from and how many bytes are found there
  const char* src = src_buffer + mut_offset;
  const size_t src_size = src_buffer_size - mut_offset;

  // If there's nothing left to write, write 0 bytes and return
  if (src_size == 0) {
    return 0;
  }

  // Perform the write, then increment our stored offset
  const size_t num_bytes_written = _chunked_write(dst, dst_size, src, src_size);
  mut_offset += num_bytes_written;
  return num_bytes_written;
}

}  // namespace datadog::impl
