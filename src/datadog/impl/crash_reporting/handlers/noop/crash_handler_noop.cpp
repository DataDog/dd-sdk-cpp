// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/crash_reporting/crash_handler.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Crash handler implementation used when the SDK is explicitly configured at
 * compile-time to include no crash-handling support.
 *
 * This implementation allows the datadog::CrashHandling API to be used normally: a
 * crash-reporting feature will be initialized, and it will operate normally, but all
 * underlying operations related to the mechanics of detecting and handling crashes will
 * be inert.
 *
 * TODO(RUM-11669): Sanity-check comments and log messages for accuracy if design
 *  changes due to modularization of SDK features.
 */
class NoopCrashHandler final : public ICrashHandler {
 public:
  NoopCrashHandler() = default;

  /**
   * Initializes the no-op crash handler implementation, doing nothing and
   * unconditionally returning true.
   */
  bool Initialize(
      DiagnosticLogger logger,
      IFilesystem& fs,
      const StoragePath& crash_storage_dir_path,
      std::string_view helper_exe_path,
      std::string_view upload_origin
  ) override {
    (void)fs;
    (void)crash_storage_dir_path;
    (void)helper_exe_path;
    (void)upload_origin;

    // Include some local log output to signal that CrashReporting API calls will do
    // nothing: this is not strictly a warning, but it should be noted for clarity
    logger.Status(
        "Crash Reporting will have no effect: SDK is compiled with DD_CRASH_MODE=noop"
    );
    return true;
  }

  void SetCrashContext(IFilesystem&, const CrashContext&) override {}
};

std::unique_ptr<ICrashHandler> CrashHandler::Create() {
  // Return an ICrashHandler implementation that will do nothing
  return std::make_unique<NoopCrashHandler>();
}

}  // namespace datadog::impl
