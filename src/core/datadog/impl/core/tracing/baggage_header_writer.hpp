// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

namespace datadog::impl {

/**
 * Writer responsible for generating the baggage headers.
 *
 * Both `TracingHeaderTypes::Datadog` and `::Tracecontext` use baggage headers.
 */
class BaggageHeaderWriter {
 public:
  BaggageHeaderWriter() = delete;

  /**
   * Appends the baggage headers to the given `out` vector, based on the data
   * wrapped by `context`.
   */
  static void AppendHeaders(TraceContext& context, std::vector<HttpHeader>& out);

 private:
  static void AppendEncodedValue(std::string_view value, std::string& out);
};

}  // namespace datadog::impl
