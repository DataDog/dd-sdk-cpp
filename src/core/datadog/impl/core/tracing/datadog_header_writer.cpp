// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/datadog_header_writer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "datadog/impl/core/tracing/http_header_writer.hpp"

namespace datadog::impl {

namespace {

constexpr const char* TRACE_ID_HEADER_NAME = "x-datadog-trace-id";
constexpr std::size_t TRACE_ID_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(TRACE_ID_HEADER_NAME);
constexpr const char* PARENT_ID_HEADER_NAME = "x-datadog-parent-id";
constexpr std::size_t PARENT_ID_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(PARENT_ID_HEADER_NAME);
constexpr const char* SAMPLING_PRIORITY_HEADER_NAME = "x-datadog-sampling-priority";
constexpr std::size_t SAMPLING_PRIORITY_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(SAMPLING_PRIORITY_HEADER_NAME);
constexpr const char* ORIGIN_HEADER_NAME = "x-datadog-origin";
constexpr std::size_t ORIGIN_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(ORIGIN_HEADER_NAME);
constexpr const char* TAGS_HEADER_NAME = "x-datadog-tags";
constexpr std::size_t TAGS_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(TAGS_HEADER_NAME);

constexpr std::string_view TRACE_ID_HI_TAG_KEY = "_dd.p.tid";
constexpr std::string_view SAMPLING_DECISION_MAKER_TAG_KEY = "_dd.p.dm";

}  // namespace

void DatadogHeaderWriter::AppendHeaders(
    TraceContext& context,
    std::optional<std::string_view> origin,
    std::vector<HttpHeader>& out
) {
  out.push_back(
      HttpHeaderWriter::CreateHeader(
          std::string_view{
              SAMPLING_PRIORITY_HEADER_NAME, SAMPLING_PRIORITY_HEADER_NAME_LENGTH
          },
          std::to_string(static_cast<int>(context.sampling_priority))
      )
  );

  out.push_back(
      HttpHeaderWriter::CreateHeader(
          std::string_view{TRACE_ID_HEADER_NAME, TRACE_ID_HEADER_NAME_LENGTH},
          context.trace_id.ToLowDecimalString()
      )
  );

  out.push_back(
      HttpHeaderWriter::CreateHeader(
          std::string_view{PARENT_ID_HEADER_NAME, PARENT_ID_HEADER_NAME_LENGTH},
          context.span_id.ToDecimalString()
      )
  );

  if (origin) {
    out.push_back(
        HttpHeaderWriter::CreateHeader(
            std::string_view{ORIGIN_HEADER_NAME, ORIGIN_HEADER_NAME_LENGTH},
            std::string(*origin)
        )
    );
  }

  // Tags
  constexpr std::size_t TRACE_ID_HIGH_HEX_LENGTH = 16;
  constexpr std::size_t MAX_SAMPLING_MECHANISM_DIGITS = 2;

  // Worst case is "_dd.p.tid=<16 hex chars>,_dd.p.dm=-xx"
  constexpr std::size_t MAX_TAGS_VALUE_LENGTH =
      TRACE_ID_HI_TAG_KEY.size() + 1 + TRACE_ID_HIGH_HEX_LENGTH + 1 +
      SAMPLING_DECISION_MAKER_TAG_KEY.size() + 2 + MAX_SAMPLING_MECHANISM_DIGITS;

  std::string tags;
  tags.reserve(MAX_TAGS_VALUE_LENGTH);

  // Append "_dd.p.tid=<trace id hi hex>"
  tags.append(TRACE_ID_HI_TAG_KEY);
  tags.push_back('=');
  tags.append(context.trace_id.ToHighHexString());

  if (IsSampled(context.sampling_priority)) {
    // Append "_dd.p.dm=-<decision maker>", for example "_dd.p.dm=-1"
    // Note: -1 above is not a negative number. The "-" is a required prefix.
    tags.push_back(',');
    tags.append(SAMPLING_DECISION_MAKER_TAG_KEY);
    tags.append("=-");
    tags.append(std::to_string(static_cast<int>(context.sampling_decision_maker)));
  }

  out.push_back(
      HttpHeaderWriter::CreateHeader(
          std::string_view{TAGS_HEADER_NAME, TAGS_HEADER_NAME_LENGTH}, std::move(tags)
      )
  );
}

}  // namespace datadog::impl
