// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

namespace datadog::impl {

/**
 * Writer responsible for generating the datadog-format headers.
 */
class DatadogHeaderWriter {
 public:
  DatadogHeaderWriter() = delete;

  /**
   * The maximum number of headers this writer may write.
   */
  static constexpr std::size_t GetMaxHeaderCount() { return 5; }

  /**
   * Appends the datadog headers to the given `out` vector, based on the data
   * wrapped by `context` and origin.
   */
  static void AppendHeaders(
      TraceContext& context,
      std::optional<std::string_view> origin,
      std::vector<HttpHeader>& out
  );
};

}  // namespace datadog::impl
