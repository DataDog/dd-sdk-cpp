// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/rum/temp_crashpad.hpp"

#if DATADOG_WITH_CRASHPAD
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "client/crashpad_client.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // GetModuleFileName
#else
#include <limits.h>  // PATH_MAX
#ifdef __APPLE__
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif
#endif

/**
 * Returns the path to the executable that is currently running this code, or an empty
 * path if unable to resolve the executable path.
 */
static std::filesystem::path get_current_executable_path() {
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
#ifdef _WIN32
  char result[MAX_PATH];
  int length = GetModuleFileName(NULL, result, MAX_PATH);
  if (length == 0 || length == MAX_PATH) {
    return "";
  }
  return std::filesystem::path(result);
#elif __APPLE__
  char buf[PATH_MAX];
  uint32_t bufsize = PATH_MAX;
  if (!_NSGetExecutablePath(buf, &bufsize)) {
    return std::filesystem::path(buf);
  }
  return "";
#else
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  if (count == -1) {
    return "";
  }
  return std::filesystem::path(std::string(result, count));
#endif
  // NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
}

/**
 * Returns the path to the crashpad_handler executable. By convention, the handler
 * executable is deployed to the same directory as the application binary.
 */
static std::filesystem::path get_crashpad_handler_path() {
  auto current_exe_path = get_current_executable_path();
#ifdef _WIN32
  return current_exe_path.parent_path() / "crashpad_handler.exe";
#else
  return current_exe_path.parent_path() / "crashpad_handler";
#endif
}

/**
 * Returns the path to the directory to use as both the crashpad database and metrics
 * directory.
 */
static std::filesystem::path get_crashpad_database_path() {
  // For now, store Crashpad data in $(pwd)/.crashpad
  return ".crashpad";
}
#endif  // DATADOG_WITH_CRASHPAD

namespace datadog::impl {

bool InitializeCrashHandler() {
#if DATADOG_WITH_CRASHPAD
  // Prepare Crashpad client options
  std::filesystem::path crashpad_handler_path = get_crashpad_handler_path();
  std::filesystem::path crashpad_database_path = get_crashpad_database_path();
  const std::string url;  // An empty URL disables uploads
  std::map<std::string, std::string> annotations;
  std::vector<std::string> arguments;
  const bool restartable = false;
  const bool asynchronous_start = false;
  std::vector<base::FilePath> attachments;

  // Attempt to start the Crashpad handler process and initialize the client library
  crashpad::CrashpadClient crashpad_client;
  return crashpad_client.StartHandler(
      base::FilePath(crashpad_handler_path),
      base::FilePath(crashpad_database_path),
      base::FilePath(crashpad_database_path),
      url,
      annotations,
      arguments,
      restartable,
      asynchronous_start,
      attachments
  );
#else
  // The SDK was compiled without Crashpad support; our crash-handling code is inert
  return true;
#endif  // DATADOG_WITH_CRASHPAD
}

}  // namespace datadog::impl
