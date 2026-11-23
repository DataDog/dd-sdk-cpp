// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#include "datadog/impl/core/tracing/w3c_header_writer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

using namespace datadog::impl;

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

static std::map<std::string_view, std::string> header_values(
    const std::vector<datadog::HttpHeader>& headers
) {
  std::map<std::string_view, std::string> values;
  for (const auto& header : headers) {
    values.emplace(header.GetName(), header.GetValue());
  }
  return values;
}

TEST_CASE("W3CHeaderWriter", "[unit][tracing][header-writers]") {
  SECTION("M write kept trace context") {
    auto context =
        make_trace_context(SamplingPriority::AutoKeep, SamplingMechanism::AgentRate);
    std::vector<datadog::HttpHeader> output;

    W3CHeaderWriter::AppendHeaders(context, "rum", output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 2);
    REQUIRE(
        headers["traceparent"] ==
        "00-00000000000004d200000000000004d2-0000000000000929-01"
    );
    REQUIRE(headers["tracestate"] == "dd=o:rum;p:0000000000000929;s:1;t.dm:-1");
  }

  SECTION("M write dropped trace context") {
    auto context =
        make_trace_context(SamplingPriority::AutoDrop, SamplingMechanism::AgentRate);
    std::vector<datadog::HttpHeader> output;

    W3CHeaderWriter::AppendHeaders(context, "rum", output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 2);
    REQUIRE(
        headers["traceparent"] ==
        "00-00000000000004d200000000000004d2-0000000000000929-00"
    );
    REQUIRE(headers["tracestate"] == "dd=o:rum;p:0000000000000929;s:0");
  }

  SECTION("M write the fallback sampling mechanism") {
    auto context =
        make_trace_context(SamplingPriority::AutoKeep, SamplingMechanism::Fallback);
    std::vector<datadog::HttpHeader> output;

    W3CHeaderWriter::AppendHeaders(context, "rum", output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 2);
    REQUIRE(
        headers["traceparent"] ==
        "00-00000000000004d200000000000004d2-0000000000000929-01"
    );
    REQUIRE(headers["tracestate"] == "dd=o:rum;p:0000000000000929;s:1;t.dm:-0");
  }

  SECTION("M omit the origin W none is supplied") {
    auto context = make_trace_context();
    std::vector<datadog::HttpHeader> output;

    W3CHeaderWriter::AppendHeaders(context, std::nullopt, output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 2);
    REQUIRE(
        headers["traceparent"] ==
        "00-00000000000004d200000000000004d2-0000000000000929-01"
    );
    REQUIRE(headers["tracestate"] == "dd=p:0000000000000929;s:1;t.dm:-1");
  }

  SECTION("M propagate a custom origin") {
    auto context = make_trace_context();
    std::vector<datadog::HttpHeader> output;

    W3CHeaderWriter::AppendHeaders(context, "synthetics", output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 2);
    REQUIRE(
        headers["traceparent"] ==
        "00-00000000000004d200000000000004d2-0000000000000929-01"
    );
    REQUIRE(headers["tracestate"] == "dd=o:synthetics;p:0000000000000929;s:1;t.dm:-1");
  }

  SECTION("M use the current span ID W no parent span exists") {
    auto context = make_trace_context();
    context.parent_span_id.reset();
    std::vector<datadog::HttpHeader> output;

    W3CHeaderWriter::AppendHeaders(context, "rum", output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 2);
    REQUIRE(
        headers["traceparent"] ==
        "00-00000000000004d200000000000004d2-0000000000000929-01"
    );
    REQUIRE(headers["tracestate"] == "dd=o:rum;p:0000000000000929;s:1;t.dm:-1");
  }

  SECTION("M serialize distinct high and low trace ID halves") {
    auto context = make_trace_context();
    context.trace_id = TraceID(0x1234567890abcdef, 42);
    std::vector<datadog::HttpHeader> output;

    W3CHeaderWriter::AppendHeaders(context, "rum", output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 2);
    REQUIRE(
        headers["traceparent"] ==
        "00-1234567890abcdef000000000000002a-0000000000000929-01"
    );
    REQUIRE(headers["tracestate"] == "dd=o:rum;p:0000000000000929;s:1;t.dm:-1");
  }

  SECTION("M zero-pad a 64-bit trace ID and span ID") {
    auto context = make_trace_context();
    context.trace_id = TraceID(0, 1);
    context.span_id = SpanID(2);
    std::vector<datadog::HttpHeader> output;

    W3CHeaderWriter::AppendHeaders(context, "rum", output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 2);
    REQUIRE(
        headers["traceparent"] ==
        "00-00000000000000000000000000000001-0000000000000002-01"
    );
    REQUIRE(headers["tracestate"] == "dd=o:rum;p:0000000000000002;s:1;t.dm:-1");
  }

  SECTION("M serialize full-width unsigned IDs") {
    auto context = make_trace_context();
    const auto maximum = std::numeric_limits<uint64_t>::max();
    context.trace_id = TraceID(maximum, maximum);
    context.span_id = SpanID(maximum);
    std::vector<datadog::HttpHeader> output;

    W3CHeaderWriter::AppendHeaders(context, "rum", output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 2);
    REQUIRE(
        headers["traceparent"] ==
        "00-ffffffffffffffffffffffffffffffff-ffffffffffffffff-01"
    );
    REQUIRE(headers["tracestate"] == "dd=o:rum;p:ffffffffffffffff;s:1;t.dm:-1");
  }
}
