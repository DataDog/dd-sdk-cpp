// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/rum.h"

#include "datadog/c/core_glue.hpp"
#include "datadog/impl/diagnostics.hpp"

namespace datadog::impl {
class Rum;
}  // namespace datadog::impl

struct dd_rum {
  std::shared_ptr<datadog::impl::Rum> impl;
  datadog::impl::DiagnosticLogger diagnostic_logger;

  explicit dd_rum(
      std::shared_ptr<datadog::impl::Rum>&& in_impl,
      const datadog::impl::DiagnosticLogger& in_diagnostic_logger
  )
      : impl(std::move(in_impl)), diagnostic_logger(in_diagnostic_logger) {}
};
