// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <optional>
#include <string>
#include <string_view>

#include "datadog/uuid.hpp"

#include "span_id.hpp"
#include "trace_id.hpp"

namespace datadog::impl {

/** The sampling priority for a trace span. */
enum class SamplingPriority : int8_t {
  /** The span is not sampled based on a manual override. */
  ManualDrop = -1,
  /** The span is not sampled based on a sampler decision. */
  AutoDrop = 0,
  /** The span is sampled based on a sampler decision. */
  AutoKeep = 1,
  /** The span is sampled based on a manual override. */
  ManualKeep = 2
};

/**
 * The mechanism used to make a span sampling decision.
 *
 * The raw values are the ones used in propagation headers like `_dd.p.dm`.
 * They do not include the `-` character, since that character is a separator
 * and not part of the value itself.
 */
enum class SamplingMechanism : uint8_t {
  /**
   * Fallback mechanism. This mechanism samples all spans. It should never be used, but
   * it's included for completion.
   */
  Fallback = 0,
  /**
   * The main decision mechanism. Although the SDK runs in an agent-less scenario, we
   * consider the SDK sampling decisions act as the agent rate in scenarios with
   * independent tracers and agent.
   */
  AgentRate = 1,
  /** Decision mechanism used when a decision is manually set by the developer. */
  Manual = 4
};

/** GraphQL request attributes. */
struct GraphQLRequestAttributes {
  /** GraphQL operation name. */
  std::optional<std::string> operation_name;

  /** GraphQL operation type. */
  std::optional<std::string> operation_type;

  /** GraphQL variables. */
  std::optional<std::string> variables;

  /** GraphQL payload. */
  std::optional<std::string> payload;

 public:
  /**
   * Initializes a `GraphQLRequestAttributes` instance with the provided parameters.
   *
   * @param operation_name GraphQL operation name, if any.
   * @param operation_type GraphQL operation type, if any.
   * @param variables GraphQL variables, if any.
   * @param payload GraphQL payload, if any.
   */
  explicit GraphQLRequestAttributes(
      std::optional<std::string_view> operation_name = std::nullopt,
      std::optional<std::string_view> operation_type = std::nullopt,
      std::optional<std::string_view> variables = std::nullopt,
      std::optional<std::string_view> payload = std::nullopt
  );
};

/** A context used to propagate trace through HTTP request headers. */
struct TraceContext {
 public:
  /** The unique identifier for the trace. */
  TraceID trace_id;

  /** The unique identifier for the span. */
  SpanID span_id;

  /** The unique identifier for the parent span, if any. */
  std::optional<SpanID> parent_span_id;

  /**
   * The sample rate used for injecting the span into a request.
   *
   * It is a value between `0.0` (drop) and `100.0` (keep), determined by the local or
   * distributed trace sampler.
   */
  float sample_rate;

  /** The sampling priority for the span. */
  SamplingPriority sampling_priority;

  /** The sampling mechanism used to make the sampling priority decision. */
  SamplingMechanism sampling_decision_maker;

  /** The unique identifier for the current RUM Session, if any. */
  std::optional<UUID> session_id;

  /** The unique identifier for the current user, if any. */
  std::optional<std::string> user_id;

  /** The unique identifier for the current account, if any. */
  std::optional<std::string> account_id;

  /** GraphQL request attributes extracted from the request, if any. */
  std::optional<GraphQLRequestAttributes> graphql;

  /**
   * Initializes a `TraceContext` instance with the provided parameters.
   *
   * @param trace_id The unique identifier for the trace.
   * @param span_id The unique identifier for the span.
   * @param parent_span_id The unique identifier for the parent span, if any.
   * @param sample_rate The sample rate used for injecting the span into a request.
   * @param sampling_priority The sampling priority for the span.
   * @param sampling_decision_maker The sampling mechanism used to make the sampling
   *  priority decision.
   * @param session_id The unique identifier for the current RUM Session, if any.
   * @param user_id The unique identifier for the current user, if any.
   * @param account_id The unique identifier for the current account, if any.
   * @param graphql GraphQL request attributes extracted from the request, if any.
   */
  explicit TraceContext(
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
  );
};

}  // namespace datadog::impl
