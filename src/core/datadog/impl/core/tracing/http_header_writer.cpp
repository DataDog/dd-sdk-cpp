// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/http_header_writer.hpp"

#include <cstddef>

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
    std::optional<std::string_view> origin,
    const DiagnosticLogger& logger
) {
  // If the request is not sampled and the injection policy requires headers only on
  // sampled requests, there's nothing to be done.
  if (IsSampled(context.sampling_priority) == false &&
      policy == TraceInjectionPolicy::Sampled) {
    return {};
  }

  using B3Encoding = B3HeaderWriter::Encoding;

  const bool write_datadog = has_header_type(header_types, TracingHeaderTypes::Datadog);
  const bool write_b3_single = has_header_type(header_types, TracingHeaderTypes::B3);
  const bool write_b3_multiple =
      has_header_type(header_types, TracingHeaderTypes::B3multi);
  const bool write_w3c =
      has_header_type(header_types, TracingHeaderTypes::Tracecontext);
  const bool write_baggage = write_datadog || write_w3c;

  std::size_t max_header_count = 0;

  if (write_datadog) {
    max_header_count += DatadogHeaderWriter::GetMaxHeaderCount();
  }

  if (write_b3_single) {
    max_header_count += B3HeaderWriter::GetMaxHeaderCount(B3Encoding::Single);
  }

  if (write_b3_multiple) {
    max_header_count += B3HeaderWriter::GetMaxHeaderCount(B3Encoding::Multiple);
  }

  if (write_w3c) {
    max_header_count += W3CHeaderWriter::GetMaxHeaderCount();
  }

  if (write_baggage) {
    max_header_count += BaggageHeaderWriter::GetMaxHeaderCount();
  }

  std::vector<HttpHeader> headers;
  headers.reserve(max_header_count);

  if (write_datadog) {
    DatadogHeaderWriter::AppendHeaders(context, origin, headers);
  }

  if (write_b3_single) {
    B3HeaderWriter::AppendHeaders(context, B3Encoding::Single, headers);
  }

  if (write_b3_multiple) {
    B3HeaderWriter::AppendHeaders(context, B3Encoding::Multiple, headers);
  }

  if (write_w3c) {
    W3CHeaderWriter::AppendHeaders(context, origin, headers);
  }

  if (write_baggage) {
    BaggageHeaderWriter::AppendHeaders(context, logger, headers);
  }

  return headers;
}

}  // namespace datadog::impl
