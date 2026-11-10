// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/datadog_header_writer.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "datadog/impl/core/tracing/http_header_factory.hpp"

namespace datadog::impl {

namespace {

constexpr std::string_view kTraceIdHeaderName = "x-datadog-trace-id";
constexpr std::string_view kParentIdHeaderName = "x-datadog-parent-id";
constexpr std::string_view kSamplingPriorityHeaderName = "x-datadog-sampling-priority";
constexpr std::string_view kOriginHeaderName = "x-datadog-origin";
constexpr std::string_view kTagsHeaderName = "x-datadog-tags";

constexpr std::string_view kTraceIdHiTagKey = "_dd.p.tid";
constexpr std::string_view kSamplingDecisionMakerTagKey = "_dd.p.dm";

}  // namespace

void DatadogHeaderWriter::AppendHeaders(
    TraceContext& context, bool is_rum, std::vector<HttpHeader>& out
) {
  out.push_back(
      HttpHeaderFactory::Create(
          kSamplingPriorityHeaderName,
          std::to_string(static_cast<int>(context.sampling_priority))
      )
  );

  out.push_back(
      HttpHeaderFactory::Create(
          kTraceIdHeaderName, context.trace_id.ToLowDecimalString()
      )
  );

  out.push_back(
      HttpHeaderFactory::Create(kParentIdHeaderName, context.span_id.ToDecimalString())
  );

  if (is_rum) {
    out.push_back(HttpHeaderFactory::Create(kOriginHeaderName, "rum"));
  }

  // Tags
  constexpr std::size_t kTraceIdHighHexLength = 16;
  constexpr std::size_t kMaxSamplingMechanismDigits = 2;

  // Worst case is "_dd.p.tid=<16 hex chars>,_dd.p.dm=-xx"
  constexpr std::size_t kMaxTagsValueLength =
      kTraceIdHiTagKey.size() + 1 + kTraceIdHighHexLength + 1 +
      kSamplingDecisionMakerTagKey.size() + 2 + kMaxSamplingMechanismDigits;

  std::string tags;
  tags.reserve(kMaxTagsValueLength);

  // Append "_dd.p.tid=<trace id hi hex>"
  tags.append(kTraceIdHiTagKey);
  tags.push_back('=');
  tags.append(context.trace_id.ToHighHexString());

  if (IsSampled(context.sampling_priority)) {
    // Append "_dd.p.dm=-<decision maker>", for example "_dd.p.dm=-1"
    // Note: -1 above is not a negative number. The "-" is a required prefix.
    tags.push_back(',');
    tags.append(kSamplingDecisionMakerTagKey);
    tags.append("=-");
    tags.append(std::to_string(static_cast<int>(context.sampling_decision_maker)));
  }

  out.push_back(HttpHeaderFactory::Create(kTagsHeaderName, std::move(tags)));
}

}  // namespace datadog::impl
