// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

namespace datadog::impl {

class DiagnosticLogger;

/**
 * Writer responsible for generating the baggage headers.
 *
 * Both `TracingHeaderTypes::Datadog` and `::Tracecontext` use baggage headers.
 */
class BaggageHeaderWriter {
 public:
  BaggageHeaderWriter() = delete;

  /**
   * The maximum number of headers this writer may write.
   */
  static constexpr std::size_t GetMaxHeaderCount() { return 1; }

  /**
   * Appends the baggage headers to the given `out` vector, based on the data
   * wrapped by `context`.
   */
  static void AppendHeaders(
      TraceContext& context,
      const DiagnosticLogger& logger,
      std::vector<HttpHeader>& out
  );

 private:
  /**
   * Append a key/value pair to the `out` string if the resulting string is smaller
   * than the size limit `MAX_BAGGAGE_SIZE`. Otherwise, `out` is left as is, and
   * `logger` logs a warning.
   *
   * The sizes are calculated based on the escaped value.
   */
  static void AppendEntryWithLogging(
      std::string_view key,
      std::string_view value,
      const DiagnosticLogger& logger,
      std::string& out
  );

  /**
   * Append a key/value pair to the `out` string if the resulting string is smaller
   * than the size limit `MAX_BAGGAGE_SIZE`. Otherwise, `out` is left as is.
   *
   * The sizes are calculated based on the escaped value.
   *
   * @returns `true` if the key/value pair is successully appended, `false` if the
   * resulting string would go over the `MAX_BAGGAGE_SIZE` size limit.
   */
  static bool AppendEntry(
      std::string_view key, std::string_view value, std::string& out
  );

  /**
   * `true` if the given `byte` should not be percent-escaped, `false` otherwise.
   */
  static bool IsUnescapedValueByte(unsigned char byte);

  /**
   * Appends `value` to `out`, percent-encoding bytes as required by the baggage
   * specification.
   */
  static void AppendEncodedValue(std::string_view value, std::string& out);
};

}  // namespace datadog::impl
