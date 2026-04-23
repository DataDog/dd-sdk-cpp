// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/filesystem_wrapper.hpp"

// This file defines private helper functions used within crash_reporting/data to read
// binary data from crash-related files.

namespace datadog::impl {

/**
 * Result of an attempt to read one or more values from a crash-related file.
 *
 * Allows us to differentiate between filesystem errors that prevent the file from being
 * read (value != FilesystemResult::OK), and situations where we the file is shorter
 * than we expected it to be, and can therefore be rejected as malformed.
 */
struct CrashFileReadResult {
  FilesystemResult value;  // Result of the read operation
  bool complete;           // Whether we successfully read all the data requested

  bool OK() const { return value == FilesystemResult::OK && complete; }
};

/**
 * Reads the next `n` bytes from the file, populating the provided buffer `dst`, which
 * must have space for `n` bytes.
 *
 * Data is usable if and only if (value == FilesystemResult::OK && complete).
 */
inline CrashFileReadResult ReadBytes(File& file, char* dst, size_t n) {
  // Consume up to n bytes from the file. File::Read is guaranteed to retry on
  // partial/interrupted reads until it hits EOF, so if we get an OK result with fewer
  // than the requested number of bytes, it's a truncated file
  auto res = file.Read(dst, n);

  // If the read failed due to a filesystem error, propagate that error
  if (res.value != FilesystemResult::OK) {
    return {res.value, false};
  }

  // Read OK: if we hit EOF before reading N bytes, signal no filesystem error, but
  // report that we couldn't read all the data that we needed to read
  if (res.bytes_read < n) {
    return {FilesystemResult::OK, false};
  }

  // Success: we read n bytes from the file without any error
  return {FilesystemResult::OK, true};
}

/**
 * Reads a single unsigned, 8-byte integer value from the file, populating `out`.
 *
 * Result value is usable if and only if (value == FilesystemResult::OK && complete).
 */
inline CrashFileReadResult ReadUInt64(File& file, uint64_t& out) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  char* dst = reinterpret_cast<char*>(&out);
  static_assert(sizeof(out) == 8, "Unexpected uint64_t size");
  return ReadBytes(file, dst, 8);
}

/**
 * Reads a single 16-byte UUID value from the file, populating `out_uuid`.
 *
 * Result value is usable if and only if (value == FilesystemResult::OK && complete).
 */
inline CrashFileReadResult ReadUUID(File& file, UUID& out_uuid) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  char* dst = reinterpret_cast<char*>(out_uuid.bytes.data());
  static_assert(sizeof(out_uuid.bytes) == 16, "Unexpected UUID buffer size");
  return ReadBytes(file, dst, 16);
}

/**
 * Reads a length-prefixed string from the file, first consuming a uint64_t length
 * value, then reading exactly that many bytes to populate `out` with string data.
 *
 * If the encoded length is less than or equal to `max_len`, the function will call
 * `std::string::resize` to allocate the required capacity in `out`, then proceed with
 * the read. If the encoded length exceeds `max_len`, the function will treat the data
 * as malformed, returning a result with `complete == false`.
 *
 * Result value is usable if and only if (value == FilesystemResult::OK && complete).
 */
inline CrashFileReadResult ReadString(File& file, std::string& out, size_t max_len) {
  uint64_t length{};
  if (auto res = ReadUInt64(file, length); !res.OK()) {
    return res;
  }
  if (length > max_len) {
    return {FilesystemResult::OK, false};
  }
  out.resize(length);
  if (length == 0) {
    return {FilesystemResult::OK, true};
  }
  return ReadBytes(file, out.data(), length);
}

}  // namespace datadog::impl
