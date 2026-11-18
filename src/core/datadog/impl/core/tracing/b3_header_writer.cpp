// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/b3_header_writer.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "datadog/impl/core/tracing/http_header_writer.hpp"

namespace datadog::impl {

namespace {

constexpr std::string_view kTraceIdHeaderName = "X-B3-TraceId";
constexpr std::string_view kSpanIdHeaderName = "X-B3-SpanId";
constexpr std::string_view kSampledHeaderName = "X-B3-Sampled";
constexpr std::string_view kB3HeaderName = "b3";

constexpr std::string_view kSampledValue = "1";
constexpr std::string_view kUnsampledValue = "0";
constexpr std::string_view kB3Separator = "-";

constexpr std::size_t kTraceIdHexLength = 32;
constexpr std::size_t kSpanIdHexLength = 16;
constexpr std::size_t kSamplingValueLength = 1;
constexpr std::size_t kMaxSingleHeaderValueLength =
    kTraceIdHexLength + kSpanIdHexLength + kSamplingValueLength +
    (2 * kB3Separator.size());

}  // namespace

void B3HeaderWriter::AppendHeaders(
    TraceContext& context, Encoding encoding, std::vector<HttpHeader>& out
) {
  const bool sampled = IsSampled(context.sampling_priority);
  const std::string_view sampled_value = sampled ? kSampledValue : kUnsampledValue;

  switch (encoding) {
    case Encoding::Multiple:
      out.push_back(
          HttpHeaderWriter::CreateHeader(kSampledHeaderName, std::string{sampled_value})
      );
      out.push_back(
          HttpHeaderWriter::CreateHeader(
              kTraceIdHeaderName, context.trace_id.ToHexString()
          )
      );
      out.push_back(
          HttpHeaderWriter::CreateHeader(
              kSpanIdHeaderName, context.span_id.ToHexString()
          )
      );
      break;

    case Encoding::Single: {
      std::string value;
      value.reserve(kMaxSingleHeaderValueLength);
      value.append(context.trace_id.ToHexString());
      value.append(kB3Separator);
      value.append(context.span_id.ToHexString());
      value.append(kB3Separator);
      value.append(sampled_value);
      out.push_back(HttpHeaderWriter::CreateHeader(kB3HeaderName, std::move(value)));
      break;
    }
  }
}

}  // namespace datadog::impl
