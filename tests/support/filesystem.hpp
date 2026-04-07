// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cinttypes>
#include <filesystem>
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

/**
 * Returns the current test process's PID, formatted as a string. Useful for tests that
 * exercise actual filesystem storage functionality and write data labeled with the
 * current PID.
 */
inline std::string GetPidString() {
#ifdef _WIN32
  const DWORD pid = GetCurrentProcessId();
#else
  const pid_t pid = getpid();
#endif
  return std::to_string(pid);
}

/**
 * Checks for leftover files and directories written by the SDK relative to the current
 * working directory. Expects to find:
 *
 * - .datadog/main/<pid>.lock
 * - .datadog/main/<pid>/ (containing no files, but may contain empty subdirs)
 *
 * Fails if:
 *
 * - .datadog/ does not exist
 * - .datadog/ contains anything other than main/
 * - main/ does not contain <pid>.lock or <pid>/
 * - <pid>/ contains any regular files, recursively
 *
 * If all assertions hold, deletes .datadog/ and all its contents.
 *
 * This function helps to keep the working directory clean in cases where tests exercise
 * Core SDK APIs directly, without a mock filesystem. Note that this function should
 * only run after all SDK state has been torn down, since an active SDK instance may
 * still be holding open file handles etc.
 */
inline void PruneDotDatadogDir() {
  // Get the path to the current working directory
  const auto application_root = std::filesystem::current_path();

  // A test that calls this function expects to have created a .datadog/ storage dir
  const auto datadog_root = application_root / ".datadog";
  REQUIRE(std::filesystem::is_directory(datadog_root));

  // There should only be one item ('main') in the root .datadog/ directory
  size_t num_entries_in_datadog_root{0};
  for (const auto& entry : std::filesystem::directory_iterator(datadog_root)) {
    REQUIRE(entry.path().filename() == "main");
    num_entries_in_datadog_root++;
  }
  REQUIRE(num_entries_in_datadog_root == 1);

  // There should only be two items (<pid>/ and <pid>.lock) in the main/ directory
  size_t num_entries_in_instance_root{0};
  for (const auto& _ : std::filesystem::directory_iterator(datadog_root / "main")) {
    num_entries_in_instance_root++;
  }
  REQUIRE(num_entries_in_instance_root == 2);

  // The SDK code under test should have created <pid>/ and <pid>.lock, within an
  // instance-level directory with a default name of 'main'
  const std::string pid_str = GetPidString();
  const auto pid_dir_path = datadog_root / "main" / pid_str;
  const auto pid_lockfile_path = datadog_root / "main" / (pid_str + ".lock");
  REQUIRE(std::filesystem::is_directory(pid_dir_path));
  REQUIRE(std::filesystem::is_regular_file(pid_lockfile_path));

  // There should be no event files left behind within <pid>/; only empty directories
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(pid_dir_path)) {
    if (entry.is_regular_file()) {
      FAIL("Event file left behind: " << entry.path());
    }
  }

  // Directory state is as it should be: we've verified that .datadog/ only contains the
  // expected results of a single test, so we can prune it to clean up after ourselves
  std::filesystem::remove_all(datadog_root);
};
