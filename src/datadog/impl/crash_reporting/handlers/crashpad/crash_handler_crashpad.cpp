// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Win32 preprocessor defines must be set before Crashpad includes
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "client/annotation.h"
#include "client/crash_report_database.h"
#include "client/crashpad_client.h"
#include "client/settings.h"

#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/crash_reporting/crash_handler.hpp"

#ifdef _WIN32
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
static crashpad::StringAnnotation<16> s_dd_tracking_consent("dd.tracking_consent");
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

namespace datadog::impl {

/**
 * Crash handler implementation that uses the Crashpad client library, in conjunction
 * with an external, out-of-process crashpad_handler executable that will be spawned by
 * the client library on Initialize().
 *
 * When a crash occurs, the Crashpad client's signal handlers detect the crash, collect
 * register state, and send an IPC notification to the handler process. The handler
 * process then captures a Breakpad-format minidump from the crashing application
 * process and uploads it to the configured intake URL.
 *
 * NOTE: COMPILING WITH DD_CRASH_MODE=crashpad IS NOT YET SUPPORTED. This is a prototype
 * implementation that does not yet report crashes to Datadog intake.
 */
class CrashpadCrashHandler final : public ICrashHandler {
 public:
  CrashpadCrashHandler() = default;

  bool Initialize(
      DiagnosticLogger logger,
      IFilesystem& fs,
      const StoragePath& crash_storage_dir_path,
      std::string_view helper_exe_path,
      std::string_view upload_origin
  ) override {
    (void)fs;

    // Prepare Crashpad client options
    std::filesystem::path crashpad_handler_path = helper_exe_path;
    if (crashpad_handler_path.empty()) {
      crashpad_handler_path = get_crashpad_handler_path();
    }
    std::filesystem::path crashpad_database_path = crash_storage_dir_path.Get();
    const std::string url =
        std::string(upload_origin) + "/crashpad-ingest-placeholder-path";
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
      logger.Error("Failed to start Crashpad handler");
      return false;
    }

    // Initialize annotation values to their defaults
    s_dd_tracking_consent.Set("pending");

    // When the Crashpad client is first initialized, it populates the configured
    // database directory with configuration metadata and other state. By default, a
    // database is configured to rate-limit uploads to once per hour, and uploads are
    // also entirely disabled by default.
    auto db = crashpad::CrashReportDatabase::Initialize(
        base::FilePath(crashpad_database_path)
    );
    if (db) {
      // Explicitly enable uploads so that new crashes will be POSTed to our upload URL
      // if not rate-limited
      if (auto* settings = db->GetSettings(); settings) {
        settings->SetUploadsEnabled(true);
      }
    }

    // Example upload behavior: if Crashpad produces a minidump with GUID
    // 617ab41b-84d0-472f-b261-bae41acb901a, and it's configured with an upload URL of
    // https://example.com/dumps/upload, along with the dd.tracking_consent annotation
    // (see s_dd_tracking_consent above), then the handler will initiate an HTTP POST
    // request equivalent to:
    //
    // clang-format off
    // ================================================================================
    // POST /dumps/upload?guid=617ab41b-84d0-472f-b261-bae41acb901a HTTP/1.1
    // Host: example.com
    // Content-Type: multipart/form-data; boundary=---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // Transfer-Encoding: chunked
    // Accept: */*
    // Content-Encoding: gzip
    // User-Agent: Crashpad/0.8.0 CFNetwork/3826.600.41 Darwin/24.6.0 (arm64)
    // Accept-Language: en-US,en;q=0.9
    // Accept-Encoding: gzip, deflate
    // Connection: keep-alive
    //
    // ---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // Content-Disposition: form-data; name="guid"
    //
    // 617ab41b-84d0-472f-b261-bae41acb901a
    // ---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // Content-Disposition: form-data; name="dd.tracking_consent"
    //
    // granted
    // ---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // Content-Disposition: form-data; name="upload_file_minidump"; filename="6af03cf2-c984-4257-a2a3-304033a95b0b.dmp"
    // Content-Type: application/octet-stream
    //
    // [... raw bytes of minidump file ...]
    // ---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // ================================================================================
    // clang-format on
    return true;
  }

  void SetCrashContext(IFilesystem& fs, const CrashContext& ctx) override {
    // We don't need to persist context to disk: we just set crashpad annotation values,
    // which the crashpad_handler executable will capture from process memory on crash
    (void)fs;

    const TrackingConsent consent = ctx.tracking_consent;
    if (consent != _tracking_consent) {
      _tracking_consent = consent;
      switch (consent) {
        case TrackingConsent::Pending:
          s_dd_tracking_consent.Set("pending");
          break;
        case TrackingConsent::Granted:
          s_dd_tracking_consent.Set("granted");
          break;
        case TrackingConsent::NotGranted:
          s_dd_tracking_consent.Set("not-granted");
          break;
      }
    }
  };

 private:
  // Tracking consent cached on last call to SetCrashContext
  TrackingConsent _tracking_consent{TrackingConsent::Pending};
};

std::unique_ptr<ICrashHandler> CrashHandler::Create() {
  return std::make_unique<CrashpadCrashHandler>();
}

}  // namespace datadog::impl
