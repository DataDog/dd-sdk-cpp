// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/crash_reporting.hpp"

#include "datadog/core.hpp"

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/features/crash_reporting/crash_reporting.hpp"

namespace datadog {

CrashReportingConfig::CrashReportingConfig() = default;

CrashReportingConfig::CrashReportingConfig(const CrashReportingConfig&) = default;
CrashReportingConfig& CrashReportingConfig::operator=(const CrashReportingConfig&) =
    default;
CrashReportingConfig::CrashReportingConfig(CrashReportingConfig&&) noexcept = default;
CrashReportingConfig& CrashReportingConfig::operator=(CrashReportingConfig&&) noexcept =
    default;

CrashReportingConfig& CrashReportingConfig::SetHandlerExePath(std::string_view value) {
  handler_exe_path = value;
  return *this;
}

CrashReporting::CrashReporting(CrashReporting::PrivateCtorTag)
    : _impl(nullptr),
      _diagnostic_handler(nullptr),
      _diagnostic_threshold(DiagnosticLevel::Error) {}

CrashReporting::CrashReporting(
    std::shared_ptr<impl::CrashReporting>&& impl,
    DiagnosticHandler diagnostic_handler,
    DiagnosticLevel diagnostic_threshold,
    CrashReporting::PrivateCtorTag
)
    : _impl(std::move(impl)),
      _diagnostic_handler(std::move(diagnostic_handler)),
      _diagnostic_threshold(diagnostic_threshold) {
  // The C++ crash reporting API doesn't currently emit any diagnostic messages, but
  // storing these values ensures that we can do so in the future without ABI changes
  (void)_diagnostic_handler;
  (void)_diagnostic_threshold;
}

CrashReporting::~CrashReporting() = default;

std::shared_ptr<CrashReporting> CrashReporting::Register(
    const std::shared_ptr<Core>& core, const CrashReportingConfig& config
) {
  // Return a no-op CrashReporting interface if called without a valid core
  if (!core || !core->_impl) {
    return std::make_shared<CrashReporting>(CrashReporting::PrivateCtorTag{});
  }

  // Extract handler executable path from config
  std::string_view handler_exe_path = config.handler_exe_path;

  // Initialize our CrashReporting feature implementation
  auto crash_reporting_impl = std::make_shared<impl::CrashReporting>(handler_exe_path);

  // Register the feature with the core, returning a no-op interface on failure
  if (!core->_impl->RegisterFeature(crash_reporting_impl)) {
    return std::make_shared<CrashReporting>(CrashReporting::PrivateCtorTag{});
  }

  // Initialize and return the API object that represents our user-facing interface
  // for the crash reporting feature
  return std::make_shared<CrashReporting>(
      std::move(crash_reporting_impl),
      core->_diagnostic_handler,
      core->_diagnostic_threshold,
      CrashReporting::PrivateCtorTag{}
  );
}

}  // namespace datadog
