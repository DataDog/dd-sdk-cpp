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
 * To enable Crash Reporting support in your application, the SDK must be compiled with
 * DD_CRASH_MODE=crashpad. At present, if you use Crashpad, your application build must
 * target C++20, and you must distribute the crashpad_handler executable alongside your
 * application.
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
   * Sets the path to the crash handler executable, which must be distributed alongside
   * your application: the SDK's Crashpad client will attempt to launch this process
   * from the provided path.
   *
   * By default, the SDK will look for a file named 'crashpad_handler' (POSIX) or
   * 'crashpad_handler.exe' (Windows) in the same directory as your application's
   * executable. If you distribute the handler executable in a different location,
   * and/or with a different name, provide the full file path to this function.
   *
   * If you provide a relative path, it will be resolved relative to the current working
   * directory.
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
