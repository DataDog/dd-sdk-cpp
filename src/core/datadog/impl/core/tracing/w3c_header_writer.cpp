// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/w3c_header_writer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "datadog/impl/core/tracing/http_header_writer.hpp"

namespace datadog::impl {

namespace {

constexpr const char* TRACEPARENT_HEADER_NAME = "traceparent";
constexpr std::size_t TRACEPARENT_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(TRACEPARENT_HEADER_NAME);
constexpr const char* TRACESTATE_HEADER_NAME = "tracestate";
constexpr std::size_t TRACESTATE_HEADER_NAME_LENGTH =
    std::char_traits<char>::length(TRACESTATE_HEADER_NAME);

constexpr std::string_view VERSION = "00";
constexpr std::string_view SAMPLED_VALUE = "01";
constexpr std::string_view UNSAMPLED_VALUE = "00";
constexpr std::string_view TRACEPARENT_SEPARATOR = "-";

// Datadog-specific tracestate keys.
constexpr std::string_view DATADOG_TRACESTATE_KEY = "dd";
constexpr std::string_view SAMPLING_KEY = "s";
constexpr std::string_view SAMPLING_DECISION_MAKER_KEY = "t.dm";
constexpr std::string_view ORIGIN_KEY = "o";
constexpr std::string_view PARENT_ID_KEY = "p";
constexpr std::string_view TRACESTATE_KEY_VALUE_SEPARATOR = ":";
constexpr std::string_view TRACESTATE_PAIR_SEPARATOR = ";";

}  // namespace

void W3CHeaderWriter::AppendHeaders(
    TraceContext& context,
    std::optional<std::string_view> origin,
    std::vector<HttpHeader>& out
) {
  const bool sampled = IsSampled(context.sampling_priority);

  std::string traceparent;
  traceparent.append(VERSION);
  traceparent.append(TRACEPARENT_SEPARATOR);
  traceparent.append(context.trace_id.ToHexString());
  traceparent.append(TRACEPARENT_SEPARATOR);
  traceparent.append(context.span_id.ToHexString());
  traceparent.append(TRACEPARENT_SEPARATOR);
  traceparent.append(sampled ? SAMPLED_VALUE : UNSAMPLED_VALUE);
  out.push_back(
      HttpHeaderWriter::CreateHeader(
          std::string_view{TRACEPARENT_HEADER_NAME, TRACEPARENT_HEADER_NAME_LENGTH},
          std::move(traceparent)
      )
  );

  // Note: all the key/value pairs in tracestate must be sorted based on the key.
  std::string tracestate;
  tracestate.append(DATADOG_TRACESTATE_KEY);
  tracestate.append("=");

  if (origin) {
    // Origin: o:<origin>
    tracestate.append(ORIGIN_KEY);
    tracestate.append(TRACESTATE_KEY_VALUE_SEPARATOR);
    tracestate.append(*origin);
    tracestate.append(TRACESTATE_PAIR_SEPARATOR);
  }

  // Parent ID: p:<span ID>
  tracestate.append(PARENT_ID_KEY);
  tracestate.append(TRACESTATE_KEY_VALUE_SEPARATOR);
  tracestate.append(context.span_id.ToHexString());

  // Sampling priority: s:<priority>
  tracestate.append(TRACESTATE_PAIR_SEPARATOR);
  tracestate.append(SAMPLING_KEY);
  tracestate.append(TRACESTATE_KEY_VALUE_SEPARATOR);
  tracestate.append(std::to_string(static_cast<int>(context.sampling_priority)));

  if (sampled) {
    // Sampling decision maker: t.dm:-<mechanism>
    tracestate.append(TRACESTATE_PAIR_SEPARATOR);
    tracestate.append(SAMPLING_DECISION_MAKER_KEY);
    tracestate.append(TRACESTATE_KEY_VALUE_SEPARATOR);
    tracestate.append("-");
    tracestate.append(
        std::to_string(static_cast<int>(context.sampling_decision_maker))
    );
  }

  out.push_back(
      HttpHeaderWriter::CreateHeader(
          std::string_view{TRACESTATE_HEADER_NAME, TRACESTATE_HEADER_NAME_LENGTH},
          std::move(tracestate)
      )
  );
}

}  // namespace datadog::impl
