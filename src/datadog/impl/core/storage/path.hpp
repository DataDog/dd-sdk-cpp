// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace datadog::impl {

/**
 * Maximum size, in bytes, for a filesystem path stored by the SDK. Includes the null
 * terminator and any multi-byte-encoded UTF-8 characters: e.g. the maximum effective
 * string length is (MAX_STORAGE_PATH_SIZE - 1) ASCII characters.
 */
static constexpr size_t MAX_STORAGE_PATH_SIZE = 512;

/**
 * Holds a string value representing a filesystem path used by the SDK.
 *
 * All StoragePath values are held internally in a fixed-size buffer, encoded as UTF-8,
 * with a null terminator always present.
 */
class StoragePath {
 public:
  /**
   * Updates the buffer to hold the specified path. The path must be absolute or
   * relative to the current directory. Paths must not contain "..".
   *
   * Returns true if the value was successfully stored; false if it exceeds the buffer
   * size or contains "..".
   */
  [[nodiscard]] bool Set(std::string_view path);

  /**
   * Updates the buffer to hold a copy of the value held in another StoragePath. Since
   * storing a value can only fail if the input value exceeds MAX_STORAGE_PATH_SIZE, and
   * since the input value is already held in another StoragePath buffer, this operation
   * can not fail.
   */
  void MustSet(const StoragePath& other);

  /**
   * Modifies the value in-place, updating the buffer to concatenate the given path
   * component onto the currently-held path. `name` must not contain "..".
   *
   * Returns true if the value was successfully updated; false if it would exceed the
   * buffer size or if `name` contains "..".
   */
  [[nodiscard]] bool Append(std::string_view name);

  /**
   * Modifies the value in-place, appending the given extension directly without a path
   * separator. Removes any trailing slash from the buffer before appending. `ext` is
   * assumed to begin with "."; e.g. supply ".lock", not "lock".
   *
   * Returns true if successful; false if would exceed buffer size or `ext` contains
   * "..".
   */
  [[nodiscard]] bool AppendExt(std::string_view ext);

  /**
   * Modifies the value in-place, updating the buffer to strip off the last path
   * component. If the path already refers to a root directory (e.g. "/" or "C:\\") or
   * the top-level component of a relative path (e.g. "foo", "foo/", ".", ""), makes no
   * changes.
   */
  void Pop();

  /**
   * Returns a string_view for the final component of the path, slash-delimited.
   */
  std::string_view Basename() const;

  /**
   * Returns a string_view for the path value currently held in the buffer.
   */
  std::string_view Get() const { return std::string_view(_buf.data(), _len); }

  /**
   * Returns a pointer to the null-terminated string data held in the buffer. Will never
   * be null.
   */
  const char* CStr() const { return _buf.data(); }

 private:
  std::array<char, MAX_STORAGE_PATH_SIZE> _buf{};  // UTF-8 bytes, always terminated
  size_t _len{0};  // Total string length in bytes, excluding null terminator
};

/**
 * Holds a string value representing a filesystem path pre-encoded in the appropriate
 * representation for calls to System APIs, always null-terminated.
 *
 * For example, this allows us to perform string conversion only on platforms that
 * require it, without heap allocations, and without conditional compilation at the
 * callsite:
 *
 *   bool DoSomething(const StoragePath& path) {
 *     PlatformPath platform_path;
 *     if (!platform_path.Encode(path.CStr())) {
 *        return false;
 *     }
 *     return CallSomeSystemAPI(platform_path.Get());
 *   }
 *
 * On Windows, Encode() converts the provided path to a null-terminated UTF-8 string,
 * returning true if successful; and Get() returns a const wchar_t* pointing to that
 * UTF-16 value.
 *
 * On all other platforms, Encode() does no work and always returns true, and Get()
 * returns a const char* pointing to the UTF-8 value last passed to Encode().
 */
class PlatformPath {
 public:
  [[nodiscard]] bool Encode(const char* utf8_path);

#ifdef _WIN32
  const wchar_t* Get() const { return _buf.data(); };
#else
  const char* Get() const { return _ptr; }
#endif

 private:
#ifdef _WIN32
  std::array<wchar_t, MAX_STORAGE_PATH_SIZE> _buf{};
#else
  const char* _ptr{nullptr};
#endif
};

}  // namespace datadog::impl
