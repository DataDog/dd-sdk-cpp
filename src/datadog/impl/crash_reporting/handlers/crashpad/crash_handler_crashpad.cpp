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

#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/crash_reporting/crash_handler.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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
static crashpad::StringAnnotation<37> s_rum_application_id("rum.application_id");
static crashpad::StringAnnotation<37> s_rum_session_id("rum.session_id");
static crashpad::StringAnnotation<37> s_rum_view_id("rum.view_id");
static crashpad::StringAnnotation<37> s_rum_action_id("rum.action_id");
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
 * register state, and send an IPC notification to the handler proces. The handler
 * process then captures a Breakpad-format minidump from the crashing application
 * process.
 *
 * NOTE: COMPILING WITH DD_CRASH_MODE=crashpad IS NOT YET SUPPORTED. This is a prototype
 * implementation that does not yet report crashes to Datadog intake. It will initialize
 * the Crashpad client and handler, crashes will be handled, and minidumps will be
 * written to `.crashpad/pending/`, but nothing will be uploaded.
 *
 * TODO(RUM-14025): Revisit Initialize() and Shutdown() when work on Crashpad support
 *  resumes.
 */
class CrashpadCrashHandler final : public ICrashHandler {
 public:
  CrashpadCrashHandler() = default;

  bool Initialize(
      DiagnosticLogger logger,
      IFilesystem& fs,
      const StoragePath& crash_storage_dir_path,
      std::string_view helper_exe_path
  ) override {
    (void)fs;

    // TODO(RUM-14025): Re-enable uploads when work on Crashpad support resumes
    const bool enable_crashpad_uploads = false;

    // Prepare Crashpad client options
    std::filesystem::path crashpad_handler_path = helper_exe_path;
    if (crashpad_handler_path.empty()) {
      crashpad_handler_path = get_crashpad_handler_path();
    }
    std::filesystem::path crashpad_database_path = crash_storage_dir_path.Get();
    // TODO(RUM-14025): To report crashes to the Datadog backend, we'd need to configure
    //  the crashpad handler to upload crash reports to an HTTP endpoint that could
    //  accept Breakpad-format minidumps, along with all relevant context in the form of
    //  annotations, and produce RUM Errors with valid (though not necessarily
    //  symbolicated) callstacks. Configuring this hardcoded upload URL is intended to
    //  facilitate interception of requests (see examples/repl/mitm_dump.py) when using
    //  the repl with mitmproxy enabled (./repl.sh), so that we can inspect Crashpad
    //  requests for debugging purposes.
    const std::string url = enable_crashpad_uploads ? "http://127.0.0.1:8080" : "";
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

    // Clear annotation values, if any were set previously
    s_rum_application_id.Set("");
    s_rum_session_id.Set("");
    s_rum_view_id.Set("");
    s_rum_action_id.Set("");

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
        settings->SetUploadsEnabled(enable_crashpad_uploads);
      }
    }

    // Example upload behavior: if Crashpad produces a minidump with GUID
    // 617ab41b-84d0-472f-b261-bae41acb901a, and it's configured with an upload URL of
    // https://example.com/dumps/upload, along with the two test annotations with values
    // 'foo' and 'bar' as noted above, then the handler will initiate an HTTP POST
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
    // Content-Disposition: form-data; name="test_annotation1"
    //
    // foo
    // ---MultipartBoundary-8kqZEVmthMNNvtlBr6K4bPibxe2jX64I---
    // Content-Disposition: form-data; name="test_annotation2"
    //
    // bar
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

  void SetRumContext(IFilesystem& fs, const RumFeatureContext& rum_ctx) override {
    // We don't need to persist context to disk: we just set crashpad annotation values,
    // which the crashpad_handler executable will capture from process memory on crash
    (void)fs;

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    char buf[37] = {0};

    if (rum_ctx.application_id != _rum_application_id) {
      _rum_application_id = rum_ctx.application_id;
      if (_rum_application_id == UUID::Zero) {
        s_rum_application_id.Set("");
      } else {
        _rum_application_id.ToBytes(buf, std::size(buf));
        s_rum_application_id.Set(buf);
      }
    }

    if (rum_ctx.session_id != _rum_session_id) {
      _rum_session_id = rum_ctx.session_id;
      if (_rum_session_id == UUID::Zero) {
        s_rum_session_id.Set("");
      } else {
        _rum_session_id.ToBytes(buf, std::size(buf));
        s_rum_session_id.Set(buf);
      }
    }

    if (rum_ctx.view_id != _rum_view_id) {
      _rum_view_id = rum_ctx.view_id;
      if (_rum_view_id == UUID::Zero) {
        s_rum_view_id.Set("");
      } else {
        _rum_view_id.ToBytes(buf, std::size(buf));
        s_rum_view_id.Set(buf);
      }
    }

    if (rum_ctx.action_id != _rum_action_id) {
      _rum_action_id = rum_ctx.action_id;
      if (_rum_action_id == UUID::Zero) {
        s_rum_action_id.Set("");
      } else {
        _rum_action_id.ToBytes(buf, std::size(buf));
        s_rum_action_id.Set(buf);
      }
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
  };

 private:
  // Values cached on last call to SetRumContext
  UUID _rum_application_id;
  UUID _rum_session_id;
  UUID _rum_view_id;
  UUID _rum_action_id;
};

std::unique_ptr<ICrashHandler> CrashHandler::Create() {
  return std::make_unique<CrashpadCrashHandler>();
}

}  // namespace datadog::impl
