// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "datadog/api.hpp"
#include "datadog/core.hpp"

namespace datadog {

// Forward declarations
namespace impl {
class CrashReporting;
}  // namespace impl

/**
 * Configures the Crash Reporting feature.
 *
 * The exact crash reporting mechanism varies depending on which crash handler
 * implementation the SDK is built with, controllable via the CMake option
 * DD_CRASH_MODE:
 *
 * - DD_CRASH_MODE=noop
 *   - Renders Crash Reporting inert: API remains usable but all calls are no-ops.
 *
 * - DD_CRASH_MODE=inprocess
 *   - This is the default option, and the option used for pre-built SDK binaries.
 *   - Captures the callstack for the crashing thread only.
 *   - Uploads reports to Datadog on the next application launch.
 *
 * - DD_CRASH_MODE=crashpad
 *   - This option is NOT YET SUPPORTED.
 *   - Includes Crashpad in the SDK build; requires targeting C++20 and distributing a
 *     datadog_crashpad_handler with your application; does not yet upload reports to
 * Datadog.
 */
struct CrashReportingConfig {
  friend class CrashReporting;
  friend class impl::CrashReporting;

 private:
  std::string handler_exe_path;

 public:
  /**
   * Initializes a new Crash Reporting configuration object with default values.
   */
  DATADOG_API CrashReportingConfig();

  // CrashReportingConfig is trivially destructible
  ~CrashReportingConfig() = default;

  // CrashReportingConfig is copyable and movable
  DATADOG_API CrashReportingConfig(const CrashReportingConfig&);
  DATADOG_API CrashReportingConfig& operator=(const CrashReportingConfig&);
  DATADOG_API CrashReportingConfig(CrashReportingConfig&&) noexcept;
  DATADOG_API CrashReportingConfig& operator=(CrashReportingConfig&&) noexcept;

  /**
   * Sets the path to the crash handler executable, overriding the default path.
   *
   * If using DD_CRASH_MODE=crashpad (not yet supported), you may supply the full path
   * to the datadog_crashpad_handler executable to be launched alongside your
   * application. By default, the SDK will look for a file named
   * 'datadog_crashpad_handler' (POSIX) or 'datadog_crashpad_handler.exe' (Windows) in
   * the same directory as your application's executable.
   *
   * If using DD_CRASH_MODE=noop or DD_CRASH_MODE=inprocess, this value is ignored.
   */
  DATADOG_API CrashReportingConfig& SetHandlerExePath(std::string_view value);
};

/**
 * Interface to the Datadog SDK's Crash Reporting feature.
 */
class CrashReporting {
 private:
  struct PrivateCtorTag {};

 public:
  // Callers should use CrashReporting::Register
  explicit CrashReporting(PrivateCtorTag);
  explicit CrashReporting(
      std::shared_ptr<impl::CrashReporting>&& impl,
      DiagnosticHandler diagnostic_handler,
      DiagnosticLevel diagnostic_threshold,
      PrivateCtorTag
  );
  DATADOG_API ~CrashReporting();

 public:
  /**
   * Registers the Crash Reporting feature with the core of the Datadog SDK.
   *
   * You MUST call this function after creating the Core and before calling
   * Core::Start().
   */
  DATADOG_API static std::shared_ptr<CrashReporting> Register(
      const std::shared_ptr<class Core>& core,
      const CrashReportingConfig& config = CrashReportingConfig()
  );

 private:
  // Forbid copying/moving: we use std::shared_ptr<CrashReporting> at the API boundary
  CrashReporting(const CrashReporting&) = delete;
  CrashReporting& operator=(const CrashReporting&) = delete;
  CrashReporting(CrashReporting&&) = delete;
  CrashReporting& operator=(CrashReporting&&) = delete;

  std::shared_ptr<impl::CrashReporting> _impl;
  DiagnosticHandler _diagnostic_handler;
  DiagnosticLevel _diagnostic_threshold;
};

}  // namespace datadog
