// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

namespace datadog::impl {

/**
 * Writer responsible for generating the W3C headers.
 *
 * See: https://www.w3.org/TR/trace-context/
 */
class W3CHeaderWriter {
 public:
  W3CHeaderWriter() = delete;

  /**
   * Appends the W3C headers to the given `out` vector, based on the data
   * wrapped by `context` and origin.
   */
  static void AppendHeaders(
      TraceContext& context,
      std::optional<std::string_view> origin,
      std::vector<HttpHeader>& out
  );
};

}  // namespace datadog::impl
