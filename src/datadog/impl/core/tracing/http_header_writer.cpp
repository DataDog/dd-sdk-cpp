// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/http_header_writer.hpp"

#include "datadog/impl/core/tracing/b3_header_writer.hpp"
#include "datadog/impl/core/tracing/baggage_header_writer.hpp"
#include "datadog/impl/core/tracing/datadog_header_writer.hpp"
#include "datadog/impl/core/tracing/w3c_header_writer.hpp"

namespace datadog::impl {

static constexpr bool has_header_type(
    TracingHeaderTypes header_types, TracingHeaderTypes required_type
) {
  return (header_types & required_type) == required_type;
}

std::vector<HttpHeader> HttpHeaderWriter::CreateHeaders(
    TraceContext& context,
    TraceInjectionPolicy policy,
    TracingHeaderTypes header_types,
    bool is_rum
) {
  // If the request is not sampled and the injection policy requires headers only on
  // sampled requests, there's nothing to be done.
  if (IsSampled(context.sampling_priority) == false &&
      policy == TraceInjectionPolicy::Sampled) {
    return {};
  }

  using B3Encoding = B3HeaderWriter::Encoding;

  std::vector<HttpHeader> headers;

  if (has_header_type(header_types, TracingHeaderTypes::Datadog)) {
    DatadogHeaderWriter::AppendHeaders(context, is_rum, headers);
  }

  if (has_header_type(header_types, TracingHeaderTypes::B3)) {
    B3HeaderWriter::AppendHeaders(context, B3Encoding::Single, headers);
  }

  if (has_header_type(header_types, TracingHeaderTypes::B3multi)) {
    B3HeaderWriter::AppendHeaders(context, B3Encoding::Multiple, headers);
  }

  if (has_header_type(header_types, TracingHeaderTypes::Tracecontext)) {
    W3CHeaderWriter::AppendHeaders(context, is_rum, headers);
  }

  if (has_header_type(header_types, TracingHeaderTypes::Tracecontext) ||
      has_header_type(header_types, TracingHeaderTypes::Datadog)) {
    BaggageHeaderWriter::AppendHeaders(context, headers);
  }

  return headers;
}

}  // namespace datadog::impl
