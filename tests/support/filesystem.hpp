// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <cinttypes>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/impl/json.hpp"

#include "support/catch.hpp"

/**
 * Given a '/'-delimited path used in tests, returns a JSON string literal representing
 * that path, properly quoted and escaped, using the appropriate path delimiter for the
 * current platform.
 */
inline std::string GetJsonLiteralForPath(std::string_view path) {
  std::string path_str(path);
#ifdef _WIN32
  std::replace(path_str.begin(), path_str.end(), '/', '\\');
#endif
  std::vector<uint8_t> buffer;
  datadog::impl::EncodeJson(buffer, path_str);
  return std::string(buffer.begin(), buffer.end());
}

/**
 * Helper function used in tests that expect diagnostic messages to be logged indicating
 * failed IFilesystem calls on specific paths.
 */
inline void RequireFilesystemError(
    const std::string& s,
    std::string_view enum_str,
    std::string_view path,
    std::string_view message_substr
) {
  // Print the full message if any assertions fail
  INFO("actual diagnostic output: " << s);

  // Line should include "error":"AccessDenied" etc.
  const std::string error_json = "\"error\":\"" + std::string(enum_str) + "\"";
  REQUIRE(s.find(error_json) != std::string::npos);

  // Line should include the path we expected the failed operation to target
  // Omit leading quote on "path" so it matches "src_path" et al.
  const std::string path_json = "path\":" + GetJsonLiteralForPath(path);
  REQUIRE(s.find(path_json) != std::string::npos);

  // Line should include the expected message substring
  REQUIRE(s.find(message_substr) != std::string::npos);
}
