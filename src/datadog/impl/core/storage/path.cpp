// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/storage/path.hpp"

#include <algorithm>

#include "datadog/impl/core/util/assert.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace datadog::impl {

// Define the canonical path separator used to concatenate paths on the current
// platform. Note that when writing paths on Windows we always use backslash, but when
// _reading_ paths on Windows we always accept either backslash or forward slash, since
// Win32 filesystem APIs treat them interchangeably.
#ifdef _WIN32
static const char PATH_SEP = '\\';
#else
static const char PATH_SEP = '/';
#endif

/**
 * Returns true if `path` contains `..` as a complete path component — i.e., a segment
 * that is exactly `..`, delimited by `/` or `\` (or at the start/end of the string).
 * A substring match (e.g. `a..b`) is not considered a path traversal attempt and
 * returns false.
 */
static bool contains_dotdot_component(std::string_view path) {
  size_t pos = 0;
  for (;;) {
    const size_t sep = path.find_first_of("/\\", pos);
    const auto component = (sep == std::string_view::npos)
                               ? path.substr(pos)
                               : path.substr(pos, sep - pos);
    if (component == "..") {
      return true;
    }
    if (sep == std::string_view::npos) {
      break;
    }
    pos = sep + 1;
  }
  return false;
}

/**
 * Given a UTF-8 string, returns true if the last character in the string is treated as
 * a path separator on the current platform. On POSIX systems, this is effectively
 * `path.endswith('/')`, but on Windows it's `path.endswith('/') || path.endswith('\\')`
 * since Windows filesystem API functions recognize either character as a separator.
 */
static bool has_trailing_slash(std::string_view path) {
  if (path.empty()) {
    return false;
  }
  const char last = path.back();
#ifdef _WIN32
  return last == '/' || last == '\\';
#else
  return last == '/';
#endif
}

/**
 * Given a UTF-8 string, returns the position of the last path separator, matching only
 * '/' on POSIX systems but matching either '/' or '\\' on Windows. Returns
 * std::string_view::npos if the string contains no valid path separators.
 */
static std::string_view::size_type find_final_slash(std::string_view path) {
#ifdef _WIN32
  size_t pos = path.size();
  while (pos-- > 0) {
    if (path[pos] == '/' || path[pos] == '\\') {
      return pos;
    }
  }
  return std::string_view::npos;
#else
  return path.find_last_of('/');
#endif
}

bool StoragePath::Set(std::string_view path) {
  // Reject if the path contains ".." as a path component, to prevent relative path
  // traversal
  if (contains_dotdot_component(path)) {
    return false;
  }

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

  // Store total string length in bytes, excluding null terminator
  _len = path.size();
  return true;
}

void StoragePath::MustSet(const StoragePath& other) {
  // Set() can only fail if the input string exceeds MAX_STORAGE_PATH_SIZE, and since
  // the input value in this case is already held in a StoragePath buffer, failure is
  // impossible
  if (!Set(other.Get())) {
    DATADOG_ASSERT(
        false, "StoragePath::Set() failed on assignment from another StoragePath"
    );
  }
}

bool StoragePath::Append(std::string_view name) {
  // Reject if new path component contains ".." as a component, to prevent relative path
  // traversal
  if (contains_dotdot_component(name)) {
    return false;
  }

  // If currently-held path is empty or ".", just set the name directly
  const std::string_view parent_path = Get();
  if (parent_path.empty() || parent_path == ".") {
    return Set(name);
  }

  // Determine if currently-held path already ends with a path separator
  const bool needs_slash = !has_trailing_slash(parent_path);

  // Account for the total number of bytes required to hold the path built from our
  // input values: parent_path + optional separator + name + null terminator
  const size_t sizeof_sep = needs_slash ? 1 : 0;
  const size_t sizeof_nul = 1;
  const size_t num_bytes = parent_path.size() + sizeof_sep + name.size() + sizeof_nul;
  if (num_bytes > _buf.size()) {
    return false;
  }

  // Start at the end of the currently-held path, add separator if needed, append name,
  // and add null terminator
  // NOLINTNEXTLINE(readability-qualified-auto)
  auto it = _buf.begin() + _len;
  if (needs_slash) {
    *it++ = PATH_SEP;
  }
  it = std::copy(name.begin(), name.end(), it);
  *it = '\0';

  // Update stored string length (count of UTF-8 bytes excluding null terminator)
  _len = parent_path.size() + sizeof_sep + name.size();
  return true;
}

bool StoragePath::AppendExt(std::string_view ext) {
  // Reject if extension contains "..", to prevent relative path traversal
  if (ext.find("..") != std::string_view::npos) {
    return false;
  }

  // If path is empty, just set the extension directly
  std::string_view current_path = Get();
  if (current_path.empty()) {
    return Set(ext);
  }

  // Remove any trailing slash(es) from the buffer before appending
  size_t adjusted_len = _len;
  while (adjusted_len > 0 &&
         has_trailing_slash(std::string_view{CStr(), adjusted_len})) {
    adjusted_len--;
  }

  // Account for total bytes: adjusted_path + ext + null terminator
  const size_t sizeof_nul = 1;
  const size_t num_bytes = adjusted_len + ext.size() + sizeof_nul;
  if (num_bytes > _buf.size()) {
    return false;
  }

  // Append extension directly after adjusted position and add null terminator
  // NOLINTNEXTLINE(readability-qualified-auto)
  auto it = _buf.begin() + adjusted_len;
  it = std::copy(ext.begin(), ext.end(), it);
  *it = '\0';

  // Update stored string length (count of UTF-8 bytes excluding null terminator)
  _len = adjusted_len + ext.size();
  return true;
}

void StoragePath::Pop() {
  // If the path ends with a trailing slash, shrink our temporary view of the string so
  // we skip past that separator when scanning backward
  std::string_view path = Get();
  if (has_trailing_slash(path)) {
    path = std::string_view{CStr(), _len - 1};
  }

  // Find the position of the last significant path separator in the string: if there is
  // none, we can pop no further - either the currently-held value is a root path (e.g.
  // "/" or "C:\\"), or it's the top-level component in a relative path (e.g. "foo",
  // "foo/", ".", or "")
  auto sep_pos = find_final_slash(path);
  if (sep_pos == std::string_view::npos) {
    return;
  }

  // Otherwise, truncate the path by replacing that separator with a null terminator
  // (and shrink the length accordingly), effectively removing the last path component
  _buf[sep_pos] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
  _len = sep_pos;
}

std::string_view StoragePath::Basename() const {
  // If the path ends with a trailing slash, shrink our temporary view of the string so
  // we skip past that separator when scanning backward
  std::string_view path = Get();
  if (has_trailing_slash(path)) {
    path = std::string_view{CStr(), _len - 1};
  }

  // Find the position of the last significant path separator in the string: if there is
  // none, then return the string as-is
  auto sep_pos = find_final_slash(path);
  if (sep_pos == std::string_view::npos) {
    return path;
  }

  // Otherwise, return a view of the contents of the string immediately following the
  // separator
  const size_t basename_len = path.size() - sep_pos - 1;
  return std::string_view{path.data() + sep_pos + 1, basename_len};
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
