// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <string>
#include <string_view>

#include "support/catch.hpp"

/**
 * Helper function used in tests that expect diagnostic messages to be logged indicating
 * failed IFilesystem calls on specific paths.
 */
void RequireFilesystemError(
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
  std::string want_path(path);
#ifdef _WIN32
  // Tests uniformly use forward slashes, but Windows paths are backslash-delimited and
  // will also be JSON-encoded in diagnostic output
  size_t pos = 0;
  while ((pos = want_path.find('/', pos)) != std::string::npos) {
    want_path.replace(pos, 1, "\\\\");
    pos += 2;
  }
#endif
  // Omit leading quote on "path" so it matches "src_path" et al.
  const std::string path_json = "path\":\"" + std::string(want_path) + "\"";
  REQUIRE(s.find(path_json) != std::string::npos);

  // Line should include the expected message substring
  REQUIRE(s.find(message_substr) != std::string::npos);
}
