// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/diagnostics.hpp"

struct dd_core {
  std::unique_ptr<datadog::impl::Core> impl;
  datadog::impl::DiagnosticLogger diagnostic_logger;

  explicit dd_core(
      std::unique_ptr<datadog::impl::Core>&& in_impl,
      datadog::impl::DiagnosticLogger&& in_diagnostic_logger
  )
      : impl(std::move(in_impl)), diagnostic_logger(in_diagnostic_logger) {}
};
