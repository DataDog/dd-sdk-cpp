// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string_view>

#include "datadog/rum.h"
#include "datadog/rum.hpp"

#include "datadog/c/core_glue.hpp"
#include "datadog/impl/rum/types.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {
class Rum;
}  // namespace datadog::impl

/**
 * Implementation of the C API's opaque RUM config handle, wrapping the C++ config that
 * it forwards setter calls to.
 *
 * The C API sets the context receiver and its userdata in separate calls, while the C++
 * config takes a single callable that binds both. The handle keeps the latest value of
 * each so that it can rebuild that callable whenever either one changes.
 */
struct dd_rum_config {
  datadog::RumConfig config;
  dd_rum_context_receiver_t context_receiver{nullptr};
  void* context_receiver_userdata{nullptr};

  explicit dd_rum_config(std::string_view application_id) : config(application_id) {}

  void UpdateContextReceiver() {
    if (context_receiver) {
      config.SetContextReceiver(
          datadog::RumContextReceiver_FromC(context_receiver, context_receiver_userdata)
      );
    } else {
      config.SetContextReceiver(nullptr);
    }
  }
};

struct dd_rum {
  std::shared_ptr<datadog::impl::Rum> impl;
  datadog::impl::DiagnosticLogger diagnostic_logger;

  explicit dd_rum(
      std::shared_ptr<datadog::impl::Rum>&& in_impl,
      const datadog::impl::DiagnosticLogger& in_diagnostic_logger
  )
      : impl(std::move(in_impl)), diagnostic_logger(in_diagnostic_logger) {}
};
