// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/crash_reporting.h"
#include "datadog/crash_reporting.hpp"

#include "datadog/c/core_glue.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {
class CrashReporting;
}  // namespace datadog::impl

/**
 * Implementation of the C API's opaque Crash Reporting config handle, wrapping the C++
 * config that it forwards setter calls to.
 */
struct dd_crash_reporting_config {
  datadog::CrashReportingConfig config;
};

struct dd_crash_reporting {
  std::shared_ptr<datadog::impl::CrashReporting> impl;
  datadog::impl::DiagnosticLogger diagnostic_logger;

  explicit dd_crash_reporting(
      std::shared_ptr<datadog::impl::CrashReporting>&& in_impl,
      const datadog::impl::DiagnosticLogger& in_diagnostic_logger
  )
      : impl(std::move(in_impl)), diagnostic_logger(in_diagnostic_logger) {}
};
