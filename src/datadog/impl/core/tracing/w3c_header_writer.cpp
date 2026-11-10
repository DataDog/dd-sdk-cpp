// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/w3c_header_writer.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "datadog/impl/core/tracing/http_header_factory.hpp"

namespace datadog::impl {

namespace {

constexpr std::string_view kTraceparentHeaderName = "traceparent";
constexpr std::string_view kTracestateHeaderName = "tracestate";
[[maybe_unused]] constexpr std::string_view kBaggageHeaderName = "baggage";

constexpr std::string_view kVersion = "00";
constexpr std::string_view kSampledValue = "01";
constexpr std::string_view kUnsampledValue = "00";
constexpr std::string_view kTraceparentSeparator = "-";

// Datadog-specific tracestate keys.
constexpr std::string_view kDatadogTracestateKey = "dd";
constexpr std::string_view kSamplingKey = "s";
constexpr std::string_view kSamplingDecisionMakerKey = "t.dm";
constexpr std::string_view kOriginKey = "o";
constexpr std::string_view kRumOrigin = "rum";
constexpr std::string_view kParentIdKey = "p";
constexpr std::string_view kTracestateKeyValueSeparator = ":";
constexpr std::string_view kTracestatePairSeparator = ";";

}  // namespace

void W3CHeaderWriter::AppendHeaders(
    TraceContext& context, bool is_rum, std::vector<HttpHeader>& out
) {
  const bool sampled = IsSampled(context.sampling_priority);

  std::string traceparent;
  traceparent.append(kVersion);
  traceparent.append(kTraceparentSeparator);
  traceparent.append(context.trace_id.ToHexString());
  traceparent.append(kTraceparentSeparator);
  traceparent.append(context.span_id.ToHexString());
  traceparent.append(kTraceparentSeparator);
  traceparent.append(sampled ? kSampledValue : kUnsampledValue);
  out.push_back(
      HttpHeaderFactory::Create(kTraceparentHeaderName, std::move(traceparent))
  );

  // Note: all the key/value pairs in tracestate must be sorted based on the key.
  std::string tracestate;
  tracestate.append(kDatadogTracestateKey);
  tracestate.append("=");

  if (is_rum) {
    // Origin: o:rum
    tracestate.append(kOriginKey);
    tracestate.append(kTracestateKeyValueSeparator);
    tracestate.append(kRumOrigin);
    tracestate.append(kTracestatePairSeparator);
  }

  // Parent ID: p:<span ID>
  tracestate.append(kParentIdKey);
  tracestate.append(kTracestateKeyValueSeparator);
  tracestate.append(context.span_id.ToHexString());

  // Sampling priority: s:<priority>
  tracestate.append(kTracestatePairSeparator);
  tracestate.append(kSamplingKey);
  tracestate.append(kTracestateKeyValueSeparator);
  tracestate.append(std::to_string(static_cast<int>(context.sampling_priority)));

  if (sampled) {
    // Sampling decision maker: t.dm:-<mechanism>
    tracestate.append(kTracestatePairSeparator);
    tracestate.append(kSamplingDecisionMakerKey);
    tracestate.append(kTracestateKeyValueSeparator);
    tracestate.append("-");
    tracestate.append(
        std::to_string(static_cast<int>(context.sampling_decision_maker))
    );
  }

  out.push_back(
      HttpHeaderFactory::Create(kTracestateHeaderName, std::move(tracestate))
  );
}

}  // namespace datadog::impl
