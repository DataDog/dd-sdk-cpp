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

#include "datadog/impl/types/assert.hpp"
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
   * @remark: We want to avoid copying header names when creatign headers, so we assume
   * the header names are valid throughout the lifetime of the program. Using "constexpr
   * std::string_view" guarantees that. An additional requirement is any `string_view`
   * passed as `name` must be null terminated, and the base string cannot have any
   * content after the end of `string_view`. The reason behind this is the C interface
   * obtians the header name by calling `GetName().data()`, so the underlying memory
   * must match what code handling C strings expect.
   *
   * @param name The header name. It must point to a non-null `string_view` meeting the
   * requirements explained in the remark above, and must remain valid throughout the
   * program's lifetime. Its contents are not copied.
   *
   * @param value The header value. This string is moved to the returned `HttpHeader`.
   *
   * @return A HttpHeader with the given name and value.
   */
  static HttpHeader CreateHeader(std::string_view name, std::string value) {
#if WITH_DATADOG_ASSERTS && !defined(NDEBUG)
    DATADOG_ASSERT(
        name.data() != nullptr && name.data()[name.size()] == '\0',
        "Header name must be null-terminated"
    );
#endif
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
