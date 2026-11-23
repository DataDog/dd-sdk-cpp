// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/core.h"
#include "datadog/core.hpp"

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/types.hpp"
#include "datadog/impl/types/diagnostics.hpp"

/**
 * Implementation of the C API's opaque config handle, wrapping the C++ config that it
 * forwards setter calls to.
 *
 * The C API sets the diagnostic handler and its userdata in separate calls, while the
 * C++ config takes a single callable that binds both. The handle keeps the latest value
 * of each so that it can rebuild that callable whenever either one changes.
 */
struct dd_core_config {
  datadog::CoreConfig config;
  dd_diagnostic_handler_t diagnostic_handler{dd_stderr_diagnostic_handler};
  void* diagnostic_handler_userdata{nullptr};

  explicit dd_core_config(datadog::CoreConfig&& in_config)
      : config(std::move(in_config)) {}

  void UpdateDiagnosticHandler() {
    if (diagnostic_handler) {
      config.SetDiagnosticHandler(
          datadog::DiagnosticHandler_FromC(
              diagnostic_handler, diagnostic_handler_userdata
          )
      );
    } else {
      config.SetDiagnosticHandler(nullptr);
    }
  }
};

struct dd_core {
  std::unique_ptr<datadog::impl::Core> impl;
  datadog::impl::DiagnosticLogger diagnostic_logger;

  explicit dd_core(
      std::unique_ptr<datadog::impl::Core>&& in_impl,
      datadog::impl::DiagnosticLogger&& in_diagnostic_logger
  )
      : impl(std::move(in_impl)), diagnostic_logger(in_diagnostic_logger) {}
};
