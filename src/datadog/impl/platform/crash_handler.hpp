// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string_view>

namespace datadog::impl {
class DiagnosticLogger;
}  // namespace datadog::impl

namespace datadog::platform {

class ICrashHandler {
 protected:
  ICrashHandler() = default;

 public:
  virtual ~ICrashHandler() = default;
  ICrashHandler(const ICrashHandler&) = delete;
  ICrashHandler& operator=(const ICrashHandler&) = delete;
  ICrashHandler(ICrashHandler&&) = default;
  ICrashHandler& operator=(ICrashHandler&&) = default;

  virtual bool Initialize() = 0;
  virtual void Shutdown() = 0;
};

namespace CrashHandler {
std::unique_ptr<ICrashHandler> Init(
    impl::DiagnosticLogger& logger, std::string_view handler_exe_path
);
}  // namespace CrashHandler

}  // namespace datadog::platform
