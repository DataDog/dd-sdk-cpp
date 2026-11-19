// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#include "datadog/impl/core/tracing/b3_header_writer.hpp"

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
    SamplingPriority priority = SamplingPriority::AutoKeep
) {
  return TraceContext(
      TraceID(1234, 1234),
      SpanID(2345),
      SpanID(5678),
      18.0F,
      priority,
      SamplingMechanism::AgentRate,
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

TEST_CASE("B3HeaderWriter single encoding", "[unit][tracing][header-writers]") {
  SECTION("M write kept trace context") {
    auto context = make_trace_context(SamplingPriority::AutoKeep);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Single, output);

    REQUIRE(output.size() == 1);
    REQUIRE(output.front().GetName() == "b3");
    REQUIRE(
        output.front().GetValue() ==
        "00000000000004d200000000000004d2-0000000000000929-1"
    );
  }

  SECTION("M write dropped trace context") {
    auto context = make_trace_context(SamplingPriority::AutoDrop);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Single, output);

    REQUIRE(output.size() == 1);
    REQUIRE(output.front().GetName() == "b3");
    REQUIRE(
        output.front().GetValue() ==
        "00000000000004d200000000000004d2-0000000000000929-0"
    );
  }

  SECTION("M write trace context without a parent span") {
    auto context = make_trace_context();
    context.parent_span_id.reset();
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Single, output);

    REQUIRE(output.size() == 1);
    REQUIRE(output.front().GetName() == "b3");
    REQUIRE(
        output.front().GetValue() ==
        "00000000000004d200000000000004d2-0000000000000929-1"
    );
  }

  SECTION("M serialize distinct high and low trace ID halves") {
    auto context = make_trace_context();
    context.trace_id = TraceID(0x1234567890abcdef, 42);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Single, output);

    REQUIRE(output.size() == 1);
    REQUIRE(output.front().GetName() == "b3");
    REQUIRE(
        output.front().GetValue() ==
        "1234567890abcdef000000000000002a-0000000000000929-1"
    );
  }

  SECTION("M zero-pad a 64-bit trace ID and span ID") {
    auto context = make_trace_context();
    context.trace_id = TraceID(0, 1);
    context.span_id = SpanID(2);
    context.parent_span_id = SpanID(3);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Single, output);

    REQUIRE(output.size() == 1);
    REQUIRE(output.front().GetName() == "b3");
    REQUIRE(
        output.front().GetValue() ==
        "00000000000000000000000000000001-0000000000000002-1"
    );
  }

  SECTION("M serialize full-width unsigned IDs") {
    auto context = make_trace_context();
    const auto maximum = std::numeric_limits<uint64_t>::max();
    context.trace_id = TraceID(maximum, maximum);
    context.span_id = SpanID(maximum);
    context.parent_span_id = SpanID(maximum - 1);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Single, output);

    REQUIRE(output.size() == 1);
    REQUIRE(output.front().GetName() == "b3");
    REQUIRE(
        output.front().GetValue() ==
        "ffffffffffffffffffffffffffffffff-ffffffffffffffff-1"
    );
  }
}

TEST_CASE("B3HeaderWriter multiple encoding", "[unit][tracing][header-writers]") {
  SECTION("M write kept trace context") {
    auto context = make_trace_context(SamplingPriority::AutoKeep);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Multiple, output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 3);
    REQUIRE(headers["X-B3-TraceId"] == "00000000000004d200000000000004d2");
    REQUIRE(headers["X-B3-SpanId"] == "0000000000000929");
    REQUIRE(headers["X-B3-Sampled"] == "1");
    REQUIRE(headers.count("X-B3-ParentSpanId") == 0);
  }

  SECTION("M write dropped trace context") {
    auto context = make_trace_context(SamplingPriority::AutoDrop);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Multiple, output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 3);
    REQUIRE(headers["X-B3-TraceId"] == "00000000000004d200000000000004d2");
    REQUIRE(headers["X-B3-SpanId"] == "0000000000000929");
    REQUIRE(headers["X-B3-Sampled"] == "0");
    REQUIRE(headers.count("X-B3-ParentSpanId") == 0);
  }

  SECTION("M write trace context without a parent span") {
    auto context = make_trace_context();
    context.parent_span_id.reset();
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Multiple, output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 3);
    REQUIRE(headers["X-B3-TraceId"] == "00000000000004d200000000000004d2");
    REQUIRE(headers["X-B3-SpanId"] == "0000000000000929");
    REQUIRE(headers["X-B3-Sampled"] == "1");
    REQUIRE(headers.count("X-B3-ParentSpanId") == 0);
  }

  SECTION("M serialize distinct high and low trace ID halves") {
    auto context = make_trace_context();
    context.trace_id = TraceID(0x1234567890abcdef, 42);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Multiple, output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 3);
    REQUIRE(headers["X-B3-TraceId"] == "1234567890abcdef000000000000002a");
    REQUIRE(headers["X-B3-SpanId"] == "0000000000000929");
    REQUIRE(headers["X-B3-Sampled"] == "1");
    REQUIRE(headers.count("X-B3-ParentSpanId") == 0);
  }

  SECTION("M zero-pad a 64-bit trace ID and span ID") {
    auto context = make_trace_context();
    context.trace_id = TraceID(0, 1);
    context.span_id = SpanID(2);
    context.parent_span_id = SpanID(3);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Multiple, output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 3);
    REQUIRE(headers["X-B3-TraceId"] == "00000000000000000000000000000001");
    REQUIRE(headers["X-B3-SpanId"] == "0000000000000002");
    REQUIRE(headers["X-B3-Sampled"] == "1");
    REQUIRE(headers.count("X-B3-ParentSpanId") == 0);
  }

  SECTION("M serialize full-width unsigned IDs") {
    auto context = make_trace_context();
    const auto maximum = std::numeric_limits<uint64_t>::max();
    context.trace_id = TraceID(maximum, maximum);
    context.span_id = SpanID(maximum);
    context.parent_span_id = SpanID(maximum - 1);
    std::vector<datadog::HttpHeader> output;

    B3HeaderWriter::AppendHeaders(context, B3HeaderWriter::Encoding::Multiple, output);

    auto headers = header_values(output);
    REQUIRE(output.size() == 3);
    REQUIRE(headers["X-B3-TraceId"] == "ffffffffffffffffffffffffffffffff");
    REQUIRE(headers["X-B3-SpanId"] == "ffffffffffffffff");
    REQUIRE(headers["X-B3-Sampled"] == "1");
    REQUIRE(headers.count("X-B3-ParentSpanId") == 0);
  }
}
