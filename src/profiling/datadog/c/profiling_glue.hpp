// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/profiling.h"

#include "datadog/c/core_glue.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {
class Profiling;
}  // namespace datadog::impl

struct dd_profiling {
  // Held weakly, so that the core's own reference is the only thing that keeps the
  // feature alive: otherwise an application that kept this handle after destroying its
  // core would hold the claim on the process-global profiler indefinitely.
  std::weak_ptr<datadog::impl::Profiling> impl;
  datadog::impl::DiagnosticLogger diagnostic_logger;

  explicit dd_profiling(
      const std::shared_ptr<datadog::impl::Profiling>& in_impl,
      const datadog::impl::DiagnosticLogger& in_diagnostic_logger
  )
      : impl(in_impl), diagnostic_logger(in_diagnostic_logger) {}
};
