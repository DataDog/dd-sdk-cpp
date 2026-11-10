// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#include "datadog/impl/core/tracing/baggage_header_writer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/types/tracing/trace_context.hpp"

using namespace datadog::impl;

static TraceContext make_trace_context(
    std::optional<std::string_view> user_id = std::nullopt,
    std::optional<std::string_view> account_id = std::nullopt
) {
  return TraceContext(
      TraceID(1234, 1234),
      SpanID(2345),
      std::nullopt,
      18.0F,
      SamplingPriority::AutoKeep,
      SamplingMechanism::AgentRate,
      std::nullopt,
      user_id,
      account_id,
      std::nullopt
  );
}

TEST_CASE("BaggageHeaderWriter fields", "[unit][tracing][header-writers]") {
  SECTION("M omit baggage W all fields are absent") {
    auto context = make_trace_context(std::nullopt, std::nullopt);
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.empty());
  }

  SECTION("M write the session ID") {
    auto context = make_trace_context(std::nullopt, std::nullopt);
    context.session_id = datadog::UUID::Parse("abcdef01-2345-6789-abcd-ef0123456789");
    REQUIRE(context.session_id.has_value());
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(
        headers.front().GetValue() == "session.id=abcdef01-2345-6789-abcd-ef0123456789"
    );
  }

  SECTION("M write the user ID") {
    auto context = make_trace_context("user-123", std::nullopt);
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(headers.front().GetValue() == "user.id=user-123");
  }

  SECTION("M write the account ID") {
    auto context = make_trace_context(std::nullopt, "account-456");
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(headers.front().GetValue() == "account.id=account-456");
  }

  SECTION("M write session and user IDs") {
    auto context = make_trace_context("user-123", std::nullopt);
    context.session_id = datadog::UUID::Parse("abcdef01-2345-6789-abcd-ef0123456789");
    REQUIRE(context.session_id.has_value());
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(
        headers.front().GetValue() ==
        "session.id=abcdef01-2345-6789-abcd-ef0123456789,user.id=user-123"
    );
  }

  SECTION("M write session and account IDs") {
    auto context = make_trace_context(std::nullopt, "account-456");
    context.session_id = datadog::UUID::Parse("abcdef01-2345-6789-abcd-ef0123456789");
    REQUIRE(context.session_id.has_value());
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(
        headers.front().GetValue() ==
        "session.id=abcdef01-2345-6789-abcd-ef0123456789,account.id=account-456"
    );
  }

  SECTION("M write user and account IDs") {
    auto context = make_trace_context("user-123", "account-456");
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(headers.front().GetValue() == "user.id=user-123,account.id=account-456");
  }

  SECTION("M write all fields") {
    auto context = make_trace_context("user-123", "account-456");
    context.session_id = datadog::UUID::Parse("abcdef01-2345-6789-abcd-ef0123456789");
    REQUIRE(context.session_id.has_value());
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(
        headers.front().GetValue() ==
        "session.id=abcdef01-2345-6789-abcd-ef0123456789,user.id=user-123,account.id="
        "account-456"
    );
  }
}

TEST_CASE("BaggageHeaderWriter encodes raw values", "[unit][tracing][header-writers]") {
  SECTION("M preserve empty values") {
    const std::string value;
    const std::string expected;
    auto context = make_trace_context(value, value);
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(
        headers.front().GetValue() == "user.id=" + expected + ",account.id=" + expected
    );
    REQUIRE(context.user_id == value);
    REQUIRE(context.account_id == value);
  }

  SECTION("M preserve permitted ASCII punctuation") {
    const std::string value =
        "!#$&'()*+-./"
        "0123456789:<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`abcdefghijklmnopqrstuvwxyz{|}~";
    const std::string expected = value;
    auto context = make_trace_context(value, value);
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(
        headers.front().GetValue() == "user.id=" + expected + ",account.id=" + expected
    );
    REQUIRE(context.user_id == value);
    REQUIRE(context.account_id == value);
  }

  SECTION("M encode delimiters and literal percent sequences") {
    const std::string value = "50% %20 %2f %GG % \",;\\\r\n\t";
    const std::string expected =
        "50%25%20%2520%20%252f%20%25GG%20%25%20%22%2C%3B%5C%0D%0A%09";
    auto context = make_trace_context(value, value);
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(
        headers.front().GetValue() == "user.id=" + expected + ",account.id=" + expected
    );
    REQUIRE(context.user_id == value);
    REQUIRE(context.account_id == value);
  }

  SECTION("M encode UTF-8 across mixed and repeated characters") {
    const std::string value = "éé,中😀 العربية 👩‍💻";
    const std::string expected =
        "%C3%A9%C3%A9%2C%E4%B8%AD%F0%9F%98%80%20"
        "%D8%A7%D9%84%D8%B9%D8%B1%D8%A8%D9%8A%D8%A9%20"
        "%F0%9F%91%A9%E2%80%8D%F0%9F%92%BB";
    auto context = make_trace_context(value, value);
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(
        headers.front().GetValue() == "user.id=" + expected + ",account.id=" + expected
    );
    REQUIRE(context.user_id == value);
    REQUIRE(context.account_id == value);
  }

  SECTION("M encode every byte according to the baggage value grammar") {
    std::string value;
    for (unsigned int byte = 0; byte < 256; ++byte) {
      value.push_back(static_cast<char>(byte));
    }
    const std::string expected =
        "%00%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"
        "%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F"
        "%20!%22#$%25&'()*+%2C-./"
        "0123456789:%3B<=>?"
        "@ABCDEFGHIJKLMNO"
        "PQRSTUVWXYZ[%5C]^_"
        "`abcdefghijklmno"
        "pqrstuvwxyz{|}~%7F"
        "%80%81%82%83%84%85%86%87%88%89%8A%8B%8C%8D%8E%8F"
        "%90%91%92%93%94%95%96%97%98%99%9A%9B%9C%9D%9E%9F"
        "%A0%A1%A2%A3%A4%A5%A6%A7%A8%A9%AA%AB%AC%AD%AE%AF"
        "%B0%B1%B2%B3%B4%B5%B6%B7%B8%B9%BA%BB%BC%BD%BE%BF"
        "%C0%C1%C2%C3%C4%C5%C6%C7%C8%C9%CA%CB%CC%CD%CE%CF"
        "%D0%D1%D2%D3%D4%D5%D6%D7%D8%D9%DA%DB%DC%DD%DE%DF"
        "%E0%E1%E2%E3%E4%E5%E6%E7%E8%E9%EA%EB%EC%ED%EE%EF"
        "%F0%F1%F2%F3%F4%F5%F6%F7%F8%F9%FA%FB%FC%FD%FE%FF";
    auto context = make_trace_context(value, value);
    std::vector<datadog::HttpHeader> headers;

    BaggageHeaderWriter::AppendHeaders(context, headers);

    REQUIRE(headers.size() == 1);
    REQUIRE(headers.front().GetName() == "baggage");
    REQUIRE(
        headers.front().GetValue() == "user.id=" + expected + ",account.id=" + expected
    );
    REQUIRE(context.user_id == value);
    REQUIRE(context.account_id == value);
  }
}
