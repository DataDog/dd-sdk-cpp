// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "client/annotation.h"
#include "client/crash_report_database.h"
#include "client/crashpad_client.h"
#include "client/settings.h"

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/crash_handler.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // GetModuleFileName
#else
#include <limits.h>  // PATH_MAX
#ifdef __APPLE__
#include <mach-o/dyld.h>  // _NSGetExecutablePath
#endif
#endif

// Crashpad Annotations: these values are stored in static, fixed-size buffers, making
// them safe to read during a crash. The Crashpad handler will automatically resolve
// these values and include them as annotations when the crash dump is uploaded.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static crashpad::StringAnnotation<64> s_test_annotation1("test_annotation1");
static crashpad::StringAnnotation<64> s_test_annotation2("test_annotation2");
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

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

namespace datadog::platform {

class CrashpadCrashHandler final : public ICrashHandler {
 public:
  explicit CrashpadCrashHandler(
      impl::DiagnosticLogger& logger, std::string_view handler_exe_path
  )
      : _logger(logger), _handler_exe_path(handler_exe_path) {}

  bool Initialize() override {
    // Prepare Crashpad client options
    std::filesystem::path crashpad_handler_path = _handler_exe_path;
    if (crashpad_handler_path.empty()) {
      crashpad_handler_path = get_crashpad_handler_path();
    }
    // TODO(RUM-14020): Figure out where Crashpad should store files
    std::filesystem::path crashpad_database_path = get_crashpad_database_path();
    const std::string url = "http://127.0.0.1:8080";
    std::map<std::string, std::string> annotations;
    std::vector<std::string> arguments;
    const bool restartable = false;
    const bool asynchronous_start = false;
    std::vector<base::FilePath> attachments;

    // Attempt to start the Crashpad handler process and initialize the client library
    crashpad::CrashpadClient crashpad_client;
    const bool started = crashpad_client.StartHandler(
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
    if (!started) {
      _logger.Error("Failed to start Crashpad handler");
      return false;
    }

    s_test_annotation1.Set("foo");
    s_test_annotation2.Set("bar");

    auto db = crashpad::CrashReportDatabase::Initialize(
        base::FilePath(crashpad_database_path)
    );
    if (db) {
      if (auto* settings = db->GetSettings(); settings) {
        settings->SetUploadsEnabled(true);
      }
    }
    return true;
  }

  void Shutdown() override {
    // Note: The crashpad handler process is designed to outlive the application
    // and continue running after SDK shutdown. We don't forcibly terminate it here.
  }

 private:
  impl::DiagnosticLogger& _logger;
  std::string _handler_exe_path;
};

std::unique_ptr<ICrashHandler> CrashHandler::Init(
    impl::DiagnosticLogger& logger, std::string_view handler_exe_path
) {
  return std::make_unique<CrashpadCrashHandler>(logger, handler_exe_path);
}

}  // namespace datadog::platform
