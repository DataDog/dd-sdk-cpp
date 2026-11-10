// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

namespace datadog::impl {

/**
 * Class responsible for generating distributed tracing HTTP headers.
 */
class HttpHeaderWriter {
 public:
  HttpHeaderWriter() = delete;

  /**
   * Creates a `HttpHeader` from the given name and value.
   *
   * @remark: The `name` backing string is not copied, therefore it must exist
   * for the entire lifetime of the header.
   */
  static HttpHeader CreateHeader(std::string_view name, std::string value) {
    return HttpHeader{name, std::move(value)};
  }

  /**
   * Creates the distributed tracing HTTP headers for the given parameters.
   *
   * @param context The trace context wrapping sampling decisions and other required
   * data.
   * @param policy The injection policy. If `Sampled`, headers are generated only if the
   * sampling decision is positive. Otherwise, if policy is `All`, headers are always
   * generated.
   * @param header_types The requested injecton header types.
   * @param origin An optional string to be used in appropriate headers. If the request
   * was intercepted by RUM, this should be set to "rum". Otherwise, it should be null.
   *
   * @return A vector of headers that should be injected in the request.
   */
  static std::vector<HttpHeader> CreateHeaders(
      TraceContext& context,
      TraceInjectionPolicy policy,
      TracingHeaderTypes header_types,
      std::optional<std::string_view> origin
  );
};

}  // namespace datadog::impl
