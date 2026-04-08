// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/crash_reporting/crash_handler.hpp"

namespace datadog::impl {

/**
 * Crash Reporting feature implementation. Initializes and manages the Crashpad crash
 * handler process, which monitors the application for crashes and uploads crash reports
 * to the Datadog backend.
 */
class CrashReporting final : public Feature {
 public:
  explicit CrashReporting(std::string_view handler_exe_path);

  FeatureId GetId() const override { return CreateFeatureId("CRSH"); }

  std::string_view GetName() const override { return "crash_reporting"; }

  std::optional<Report> UploadThread_PrepareReport(
      const HttpContext& context, BatchReader& reader
  ) override;

  std::optional<std::function<void(const FeatureMessage&)>>
  MakeMessageHandler() override;

 protected:
  /**
   * Responds to SDK start by initializing the Crashpad crash handler process.
   * If initialization fails, logs an error but does not prevent SDK startup.
   */
  void Start() override;

  /**
   * Responds to SDK stop by performing any necessary cleanup. Note that the
   * crash handler process continues running after SDK shutdown.
   */
  void Stop() override;

 private:
  // Configuration: path to the crashpad_handler executable
  const std::string _handler_exe_path;

  // Platform-specific crash handler implementation
  std::unique_ptr<platform::ICrashHandler> _crash_handler;

  // Last RUM context forwarded to the crash handler; used to suppress redundant
  // SetRumContext calls when the context hasn't actually changed
  std::optional<RumFeatureContext> _last_rum_ctx;
};

}  // namespace datadog::impl
