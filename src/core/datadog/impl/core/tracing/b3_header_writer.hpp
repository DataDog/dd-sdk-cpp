// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

namespace datadog::impl {

/**
 * Writer responsible for generating the Open Telemetry B3 headers headers.
 */
class B3HeaderWriter {
 public:
  /// B3 header encoding.
  enum class Encoding {
    /// Use multiple headers. See
    /// https://github.com/openzipkin/b3-propagation#multiple-headers
    Multiple,
    /// Use single header. See
    /// https://github.com/openzipkin/b3-propagation#single-header
    Single
  };

  B3HeaderWriter() = delete;

  static constexpr std::size_t GetMaxHeaderCount(Encoding encoding) {
    return encoding == Encoding::Single ? 1 : 4;
  }

  /**
   * Appends the B3 headers to the given `out` vector, based on the data
   * wrapped by `context` and the given encoding.
   */
  static void AppendHeaders(
      TraceContext& context, Encoding encoding, std::vector<HttpHeader>& out
  );
};

}  // namespace datadog::impl
