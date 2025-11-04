// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/logging.h"
#include "datadog/logging.hpp"
#include "diagnostics.hpp"

namespace datadog::impl {
class Logger;
class Logging;
}  // namespace datadog::impl

struct dd_logger {
  std::unique_ptr<datadog::impl::Logger> impl;
  datadog::impl::DiagnosticLogger diagnostic_logger;

  explicit dd_logger(
      std::unique_ptr<datadog::impl::Logger>&& in_impl,
      const datadog::impl::DiagnosticLogger& in_diagnostic_logger
  )
      : impl(std::move(in_impl)), diagnostic_logger(in_diagnostic_logger) {}
};

struct dd_logging {
  std::shared_ptr<datadog::impl::Logging> impl;
  datadog::impl::DiagnosticLogger diagnostic_logger;

  explicit dd_logging(
      std::shared_ptr<datadog::impl::Logging>&& in_impl,
      const datadog::impl::DiagnosticLogger& in_diagnostic_logger
  )
      : impl(std::move(in_impl)), diagnostic_logger(in_diagnostic_logger) {}
};
