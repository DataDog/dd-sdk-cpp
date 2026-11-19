// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/b3_header_writer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "datadog/impl/core/tracing/http_header_writer.hpp"

namespace datadog::impl {

namespace {

constexpr const char* TRACE_ID_HEADER_NAME = "X-B3-TraceId";
constexpr std::size_t TRACE_ID_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(TRACE_ID_HEADER_NAME);
constexpr const char* SPAN_ID_HEADER_NAME = "X-B3-SpanId";
constexpr std::size_t SPAN_ID_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(SPAN_ID_HEADER_NAME);
constexpr const char* SAMPLED_HEADER_NAME = "X-B3-Sampled";
constexpr std::size_t SAMPLED_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(SAMPLED_HEADER_NAME);
constexpr const char* B3_HEADER_NAME = "b3";
constexpr std::size_t B3_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(B3_HEADER_NAME);

constexpr std::string_view SAMPLED_VALUE = "1";
constexpr std::string_view UNSAMPLED_VALUE = "0";
constexpr std::string_view B3_SEPARATOR = "-";

constexpr std::size_t TRACE_ID_HEX_LENGTH = 32;
constexpr std::size_t SPAN_ID_HEX_LENGTH = 16;
constexpr std::size_t SAMPLING_VALUE_LENGTH = 1;
constexpr std::size_t MAX_SINGLE_HEADER_VALUE_LENGTH =
    TRACE_ID_HEX_LENGTH + SPAN_ID_HEX_LENGTH + SAMPLING_VALUE_LENGTH +
    (2 * B3_SEPARATOR.size());

}  // namespace

void B3HeaderWriter::AppendHeaders(
    TraceContext& context, Encoding encoding, std::vector<HttpHeader>& out
) {
  const bool sampled = IsSampled(context.sampling_priority);
  const std::string_view sampled_value = sampled ? SAMPLED_VALUE : UNSAMPLED_VALUE;

  switch (encoding) {
    case Encoding::Multiple:
      out.push_back(
          HttpHeaderWriter::CreateHeader(
              std::string_view{SAMPLED_HEADER_NAME, SAMPLED_HEADER_NAME_LENGTH},
              std::string{sampled_value}
          )
      );
      out.push_back(
          HttpHeaderWriter::CreateHeader(
              std::string_view{TRACE_ID_HEADER_NAME, TRACE_ID_HEADER_NAME_LENGTH},
              context.trace_id.ToHexString()
          )
      );
      out.push_back(
          HttpHeaderWriter::CreateHeader(
              std::string_view{SPAN_ID_HEADER_NAME, SPAN_ID_HEADER_NAME_LENGTH},
              context.span_id.ToHexString()
          )
      );
      break;

    case Encoding::Single: {
      std::string value;
      value.reserve(MAX_SINGLE_HEADER_VALUE_LENGTH);
      value.append(context.trace_id.ToHexString());
      value.append(B3_SEPARATOR);
      value.append(context.span_id.ToHexString());
      value.append(B3_SEPARATOR);
      value.append(sampled_value);
      out.push_back(
          HttpHeaderWriter::CreateHeader(
              std::string_view{B3_HEADER_NAME, B3_HEADER_NAME_LENGTH}, std::move(value)
          )
      );
      break;
    }
  }
}

}  // namespace datadog::impl
