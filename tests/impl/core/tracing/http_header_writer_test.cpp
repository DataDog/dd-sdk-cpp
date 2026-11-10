// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#include "datadog/impl/core/tracing/http_header_writer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

using datadog::TraceInjectionPolicy;
using datadog::TracingHeaderTypes;
using namespace datadog::impl;

constexpr auto kAllHeaderTypes = TracingHeaderTypes::Datadog | TracingHeaderTypes::B3 |
                                 TracingHeaderTypes::B3multi |
                                 TracingHeaderTypes::Tracecontext;

static TraceContext make_trace_context(
    SamplingPriority priority = SamplingPriority::AutoKeep,
    SamplingMechanism mechanism = SamplingMechanism::AgentRate
) {
  return TraceContext(
      TraceID(1234, 1234),
      SpanID(2345),
      SpanID(5678),
      18.0F,
      priority,
      mechanism,
      datadog::UUID::Parse("abcdef01-2345-6789-abcd-ef0123456789"),
      "user-123",
      "account-456",
      std::nullopt
  );
}

static std::multiset<std::string_view> header_names(
    const std::vector<datadog::HttpHeader>& headers
) {
  std::multiset<std::string_view> names;
  for (const auto& header : headers) {
    names.insert(header.GetName());
  }
  return names;
}

static std::map<std::string_view, std::string> header_values(
    const std::vector<datadog::HttpHeader>& headers
) {
  std::map<std::string_view, std::string> values;
  for (const auto& header : headers) {
    values.emplace(header.GetName(), header.GetValue());
  }
  return values;
}

TEST_CASE("HttpHeaderWriter header selection", "[unit][tracing][header-writers]") {
  SECTION("M include Datadog headers and baggage W Datadog is requested") {
    auto context = make_trace_context();

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, TracingHeaderTypes::Datadog, std::nullopt
    );

    const std::multiset<std::string_view> expected{
        "x-datadog-sampling-priority",
        "x-datadog-trace-id",
        "x-datadog-parent-id",
        "x-datadog-tags",
        "baggage"
    };
    REQUIRE(header_names(headers) == expected);
  }

  SECTION("M include only B3 single headers W B3 is requested") {
    auto context = make_trace_context();

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, TracingHeaderTypes::B3, std::nullopt
    );

    const std::multiset<std::string_view> expected{"b3"};
    REQUIRE(header_names(headers) == expected);
  }

  SECTION("M include only B3 multiple headers W B3multi is requested") {
    auto context = make_trace_context();

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, TracingHeaderTypes::B3multi, std::nullopt
    );

    const std::multiset<std::string_view> expected{
        "X-B3-TraceId", "X-B3-SpanId", "X-B3-Sampled", "X-B3-ParentSpanId"
    };
    REQUIRE(header_names(headers) == expected);
  }

  SECTION("M include W3C headers and baggage W trace context is requested") {
    auto context = make_trace_context();

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context,
        TraceInjectionPolicy::All,
        TracingHeaderTypes::Tracecontext,
        std::nullopt
    );

    const std::multiset<std::string_view> expected{
        "traceparent", "tracestate", "baggage"
    };
    REQUIRE(header_names(headers) == expected);
  }

  SECTION("M include default headers with baggage exactly once") {
    auto context = make_trace_context();

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, TracingHeaderTypes::Default, std::nullopt
    );

    const std::multiset<std::string_view> expected{
        "x-datadog-sampling-priority",
        "x-datadog-trace-id",
        "x-datadog-parent-id",
        "x-datadog-tags",
        "traceparent",
        "tracestate",
        "baggage"
    };
    REQUIRE(header_names(headers) == expected);
  }

  SECTION("M include both B3 encodings without origin or baggage") {
    auto context = make_trace_context();

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context,
        TraceInjectionPolicy::All,
        TracingHeaderTypes::B3 | TracingHeaderTypes::B3multi,
        "rum"
    );

    const std::multiset<std::string_view> expected{
        "b3", "X-B3-TraceId", "X-B3-SpanId", "X-B3-Sampled", "X-B3-ParentSpanId"
    };
    REQUIRE(header_names(headers) == expected);
  }

  SECTION("M include all requested formats with baggage exactly once") {
    auto context = make_trace_context();

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, kAllHeaderTypes, std::nullopt
    );

    const std::multiset<std::string_view> expected{
        "x-datadog-sampling-priority",
        "x-datadog-trace-id",
        "x-datadog-parent-id",
        "x-datadog-tags",
        "b3",
        "X-B3-TraceId",
        "X-B3-SpanId",
        "X-B3-Sampled",
        "X-B3-ParentSpanId",
        "traceparent",
        "tracestate",
        "baggage"
    };
    REQUIRE(header_names(headers) == expected);
  }

  SECTION("M return no headers W no header types are requested") {
    auto context = make_trace_context();

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context,
        TraceInjectionPolicy::All,
        static_cast<TracingHeaderTypes>(0),
        std::nullopt
    );

    REQUIRE(headers.empty());
  }
}

TEST_CASE("HttpHeaderWriter injection policy", "[unit][tracing][header-writers]") {
  SECTION("M include headers for kept traces W policy is All") {
    auto context =
        make_trace_context(SamplingPriority::AutoKeep, SamplingMechanism::AgentRate);

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, kAllHeaderTypes, std::nullopt
    );

    REQUIRE(headers.size() == 12);
    REQUIRE(header_names(headers).count("baggage") == 1);
  }

  SECTION("M include headers for dropped traces W policy is All") {
    auto context =
        make_trace_context(SamplingPriority::AutoDrop, SamplingMechanism::AgentRate);

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, kAllHeaderTypes, std::nullopt
    );

    REQUIRE(headers.size() == 12);
    REQUIRE(header_names(headers).count("baggage") == 1);
  }

  SECTION("M include headers for kept traces W policy is Sampled") {
    auto context =
        make_trace_context(SamplingPriority::AutoKeep, SamplingMechanism::AgentRate);

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::Sampled, kAllHeaderTypes, std::nullopt
    );

    REQUIRE(headers.size() == 12);
    REQUIRE(header_names(headers).count("baggage") == 1);
  }

  SECTION("M omit headers for dropped traces W policy is Sampled") {
    auto context =
        make_trace_context(SamplingPriority::AutoDrop, SamplingMechanism::AgentRate);

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::Sampled, kAllHeaderTypes, std::nullopt
    );

    REQUIRE(headers.empty());
  }
}

TEST_CASE("HttpHeaderWriter origin and baggage", "[unit][tracing][header-writers]") {
  SECTION("M forward the RUM origin to Datadog and W3C") {
    auto context = make_trace_context();

    const auto output = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, TracingHeaderTypes::Default, "rum"
    );

    auto headers = header_values(output);
    REQUIRE(output.size() == 8);
    REQUIRE(headers["x-datadog-origin"] == "rum");
    REQUIRE(headers["tracestate"].find("o:rum;") != std::string::npos);
  }

  SECTION("M forward the custom origin to Datadog and W3C") {
    auto context = make_trace_context();

    const auto output = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, TracingHeaderTypes::Default, "synthetics"
    );

    auto headers = header_values(output);
    REQUIRE(output.size() == 8);
    REQUIRE(headers["x-datadog-origin"] == "synthetics");
    REQUIRE(headers["tracestate"].find("o:synthetics;") != std::string::npos);
  }

  SECTION("M omit origin fields W no origin is supplied") {
    auto context = make_trace_context();

    const auto output = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, TracingHeaderTypes::Default, std::nullopt
    );

    auto headers = header_values(output);
    REQUIRE(output.size() == 7);
    REQUIRE(headers.count("x-datadog-origin") == 0);
    REQUIRE(headers["tracestate"].find("o:") == std::string::npos);
  }

  SECTION("M omit baggage W the context has no baggage fields") {
    auto context = make_trace_context();
    context.session_id.reset();
    context.user_id.reset();
    context.account_id.reset();

    const auto headers = HttpHeaderWriter::CreateHeaders(
        context, TraceInjectionPolicy::All, TracingHeaderTypes::Default, std::nullopt
    );

    const std::multiset<std::string_view> expected{
        "x-datadog-sampling-priority",
        "x-datadog-trace-id",
        "x-datadog-parent-id",
        "x-datadog-tags",
        "traceparent",
        "tracestate"
    };
    REQUIRE(header_names(headers) == expected);
  }
}
