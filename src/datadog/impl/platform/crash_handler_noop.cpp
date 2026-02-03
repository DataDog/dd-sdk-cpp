// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/crash_handler.hpp"

namespace datadog::platform {

class NoopCrashHandler final : public ICrashHandler {
 public:
  explicit NoopCrashHandler(impl::DiagnosticLogger& logger) {
    (void)logger;
    // No-op crash handler - does nothing
  }

  bool Initialize() override { return true; }

  void Shutdown() override {}
};

std::unique_ptr<ICrashHandler> CrashHandler::Init(
    impl::DiagnosticLogger& logger, std::string_view handler_exe_path
) {
  (void)handler_exe_path;
  return std::make_unique<NoopCrashHandler>(logger);
}

}  // namespace datadog::platform
