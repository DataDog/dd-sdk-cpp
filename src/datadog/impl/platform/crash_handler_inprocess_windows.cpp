// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/crash_handler.hpp"

namespace datadog::platform {

// TODO: Phase 5 - Windows in-process crash handler implementation
// This is a placeholder that will be replaced with the full Windows implementation.

class InProcessCrashHandler final : public ICrashHandler {
 public:
  explicit InProcessCrashHandler(impl::DiagnosticLogger& logger) : _logger(logger) {}

  bool Initialize() override {
    _logger.Error("In-process crash handler not yet implemented for Windows");
    return false;
  }

  void Shutdown() override {
    // No-op for placeholder
  }

 private:
  impl::DiagnosticLogger& _logger;
};

std::unique_ptr<ICrashHandler> CrashHandler::Init(
    impl::DiagnosticLogger& logger, std::string_view handler_exe_path
) {
  (void)handler_exe_path;
  return std::make_unique<InProcessCrashHandler>(logger);
}

}  // namespace datadog::platform
