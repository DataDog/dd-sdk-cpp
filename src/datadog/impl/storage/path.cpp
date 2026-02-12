// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/path.hpp"

#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace datadog::impl {

bool StoragePath::Set(std::string_view path) {
  // Account for the total number of bytes required to hold the input value: number of
  // bytes in the string, plus a null terminator
  const size_t sizeof_nul = 1;
  const size_t num_bytes = path.size() + sizeof_nul;
  if (num_bytes > _buf.size()) {
    return false;
  }

  // Copy the path string to our buffer, then append null terminator
  // NOLINTNEXTLINE(readability-qualified-auto)
  auto it = std::copy(path.begin(), path.end(), _buf.begin());
  *it = '\0';
  _len = path.size();
  return true;
}

bool StoragePath::Join(std::string_view parent_path, std::string_view name) {
  // Reject if either path component contains "..", to prevent relative path traversal
  if (parent_path.find("..") != std::string_view::npos) {
    return false;
  }
  if (name.find("..") != std::string_view::npos) {
    return false;
  }

  // If parent_path is empty or ".", just set the name directly without any prefix
  if (parent_path.empty() || parent_path == ".") {
    return Set(name);
  }

  // Join path components using the appropriate path separator for the platform
#ifdef _WIN32
  const char sep = '\\';
#else
  const char sep = '/';
#endif

  // Determine if parent_path already ends with a path separator
  bool has_trailing_slash = false;
  if (!parent_path.empty()) {
    const char last_char = parent_path.back();
#ifdef _WIN32
    // On Windows, both forward slash and backslash are valid separators
    has_trailing_slash = (last_char == '/' || last_char == '\\');
#else
    // On non-Windows platforms, only forward slash is a separator
    has_trailing_slash = (last_char == '/');
#endif
  }

  // Account for the total number of bytes required to hold the path built from our
  // input values: parent_path + optional separator + name + null terminator
  const size_t sizeof_sep = has_trailing_slash ? 0 : 1;
  const size_t sizeof_nul = 1;
  const size_t num_bytes = parent_path.size() + sizeof_sep + name.size() + sizeof_nul;
  if (num_bytes > _buf.size()) {
    return false;
  }

  // Copy parent_path to our buffer, add separator only if parent_path doesn't already
  // have a trailing slash, append name, and add null terminator
  // NOLINTNEXTLINE(readability-qualified-auto)
  auto it = std::copy(parent_path.begin(), parent_path.end(), _buf.begin());
  if (!has_trailing_slash) {
    *it++ = sep;
  }
  it = std::copy(name.begin(), name.end(), it);
  *it = '\0';

  // Update stored string length (count of UTF-8 bytes excluding null terminator)
  _len = parent_path.size() + sizeof_sep + name.size();
  return true;
}

bool PlatformPath::Encode(const char* utf8_path) {
#ifdef _WIN32
  // Determine how many UTF-16 code units are required to represent the given UTF-8
  // input string: setting cbMultiByte to -1 causes MultiByteToWideChar to treat the
  // string as null-terminated, meaning that the resulting size includes the null
  // terminator. Including MB_ERR_INVALID_CHARS ensures that input strings with invalid
  // UTF-8 sequences will be rejected outright.
  const DWORD flags = MB_ERR_INVALID_CHARS;
  const int cb_multi_byte = -1;
  const int num_utf16_chars =
      MultiByteToWideChar(CP_UTF8, flags, utf8_path, cb_multi_byte, nullptr, 0);

  // If the input string is not valid UTF-8 (or the call fails), abort
  if (num_utf16_chars == 0) {
    return false;
  }

  // If we don't have sufficient buffer space to store our string in UTF-16 format, fail
  // (num_utf16_chars accounts for null terminator)
  if (static_cast<size_t>(num_utf16_chars) > _buf.size()) {
    return false;
  }

  // Perform the conversion, writing into our wchar_t buffer
  const int result = MultiByteToWideChar(
      CP_UTF8, flags, utf8_path, cb_multi_byte, _buf.data(), num_utf16_chars
  );
  return result == num_utf16_chars;
#else
  // POSIX treats a file path as an opaque sequence of bytes, and we assume UTF-8 by
  // convention on macOS and Linux; so we can use our underlying StoragePath buffer
  // directly, without requiring conversion
  _ptr = utf8_path;
  return true;
#endif
}

}  // namespace datadog::impl
