// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

namespace datadog::impl {

class W3CHeaderWriter {
 public:
  W3CHeaderWriter() = delete;

  static void AppendHeaders(
      TraceContext& context, bool is_rum, std::vector<HttpHeader>& out
  );
};

}  // namespace datadog::impl
