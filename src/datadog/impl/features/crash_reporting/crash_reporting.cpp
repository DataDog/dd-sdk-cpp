// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/crash_reporting/crash_reporting.hpp"

#include "datadog/impl/assert.hpp"
#include "datadog/impl/core/writer.hpp"
#include "datadog/impl/features/crash_reporting/temp_crashpad.hpp"

namespace datadog::impl {

CrashReporting::CrashReporting(std::string_view handler_exe_path)
    : _handler_exe_path(handler_exe_path), _initialized(false) {}

std::optional<Report> CrashReporting::UploadThread_PrepareReport(
    const HttpContext& context, BatchReader& reader
) {
  // The Crash Reporting feature implementation does not currently generate or upload
  // any events in-process
  (void)context;
  (void)reader;
  return std::nullopt;
}

void CrashReporting::Start() {
  DATADOG_ASSERT(_scope, "CrashReporting::Start called without valid FeatureScope");
  DATADOG_ASSERT(
      !_initialized, "CrashReporting::Start called when already initialized"
  );

  // Attempt to start Crashpad so we can verify that our build process has successfully
  // linked the Crashpad client library and bundled the crashpad_handler executable

  // TODO(RUM-12207): Should this run on feature registration instead of on SDK start?
  //  Crashpad init is necessarily static (it happens once per process and can't be
  //  undone), and starting the handler as early as possible seems ideal for having the
  //  best chance of catching early crashes.

  // TODO(RUM-12207): Define a proper (Crashpad-agnostic) interface for initializing,
  //  configuring, and controlling crash-reporting functionality within the SDK

  static bool has_called_initialize_crash_handler = false;
  if (!has_called_initialize_crash_handler) {
    // Ensure that we only initialize Crashpad once in any given process
    _initialized = InitializeCrashHandler(_handler_exe_path);
    has_called_initialize_crash_handler = true;

    // Log a status message to indicate that the Crashpad client has been initialized;
    // or log an error if initialization failed
    if (_initialized) {
      _scope->diagnostic_logger.Status("Crash handler initialized");
    } else {
      // TODO(RUM-12207): Surface errors from function return, and/or capture and
      // redirect log output from Crashpad?
      _scope->diagnostic_logger.Error("Crash handler initialization failed");
    }
  }
}

void CrashReporting::Stop() {
  // Note: The crashpad handler process is designed to outlive the application
  // and continue running after SDK shutdown. We don't forcibly terminate it here.
}

}  // namespace datadog::impl
