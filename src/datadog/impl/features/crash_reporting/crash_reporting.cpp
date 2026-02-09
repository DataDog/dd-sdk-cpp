// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/features/crash_reporting/crash_reporting.hpp"

#include "datadog/impl/assert.hpp"
#include "datadog/impl/core/writer.hpp"

namespace datadog::impl {

CrashReporting::CrashReporting(std::string_view handler_exe_path)
    : _handler_exe_path(handler_exe_path) {}

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

  // Initialize the crash handler. This is static per process - it happens once and
  // can't be undone. Starting the handler as early as possible gives us the best chance
  // of catching early crashes.

  static bool has_called_initialize = false;
  if (!has_called_initialize) {
    has_called_initialize = true;

    _crash_handler =
        platform::CrashHandler::Init(_scope->diagnostic_logger, _handler_exe_path);
    if (!_crash_handler) {
      _scope->diagnostic_logger.Error("Failed to create crash handler");
      return;
    }

    if (_crash_handler->Initialize()) {
      _scope->diagnostic_logger.Status("Crash handler initialized");
    } else {
      _scope->diagnostic_logger.Error("Crash handler initialization failed");
    }
  }
}

void CrashReporting::Stop() {
  if (_crash_handler) {
    _crash_handler->Shutdown();
  }
}

}  // namespace datadog::impl
