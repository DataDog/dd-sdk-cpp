// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "trace_context.hpp"

namespace datadog::impl {

GraphQLRequestAttributes::GraphQLRequestAttributes(
    std::optional<std::string_view> operation_name,
    std::optional<std::string_view> operation_type,
    std::optional<std::string_view> variables,
    std::optional<std::string_view> payload
)
    : operation_name(operation_name),
      operation_type(operation_type),
      variables(variables),
      payload(payload) {}

TraceContext::TraceContext(
    const TraceID& trace_id,
    const SpanID& span_id,
    const std::optional<SpanID>& parent_span_id,
    float sample_rate,
    SamplingPriority sampling_priority,
    SamplingMechanism sampling_decision_maker,
    const std::optional<UUID>& session_id,
    const std::optional<std::string_view> user_id,
    const std::optional<std::string_view> account_id,
    const std::optional<GraphQLRequestAttributes>& graphql
)
    : trace_id(trace_id),
      span_id(span_id),
      parent_span_id(parent_span_id),
      sample_rate(sample_rate),
      sampling_priority(sampling_priority),
      sampling_decision_maker(sampling_decision_maker),
      session_id(session_id),
      user_id(user_id),
      account_id(account_id),
      graphql(graphql) {}

}  // namespace datadog::impl
