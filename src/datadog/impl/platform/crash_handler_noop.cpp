// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/crash_handler.hpp"

namespace datadog::platform {

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
  explicit NoopCrashHandler(impl::DiagnosticLogger& logger) : _logger(logger) {}

  /**
   * Initializes the no-op crash handler implementation, doing nothing and
   * unconditionally returning true.
   */
  bool Initialize() override {
    // Include some local log output to signal that CrashReporting API calls will do
    // nothing: this is not strictly a warning, but it should be noted for clarity
    _logger.Status(
        "Crash Reporting will have no effect: SDK is compiled with DD_CRASH_MODE=noop"
    );
    return true;
  }

  /**
   * Does nothing.
   */
  void Shutdown() override {}

 private:
  impl::DiagnosticLogger _logger;
};

std::unique_ptr<ICrashHandler> CrashHandler::Init(
    impl::DiagnosticLogger& logger, std::string_view handler_exe_path
) {
  // datadog::CrashReportingConfig is universal by design; some of the common options
  // used to initialize ICrashHandler are irrelevant to this implementation
  (void)handler_exe_path;

  // Return an ICrashHandler implementation that will do nothing
  return std::make_unique<NoopCrashHandler>(logger);
}

}  // namespace datadog::platform
