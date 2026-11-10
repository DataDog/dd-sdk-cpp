// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/baggage_header_writer.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "datadog/impl/core/tracing/http_header_writer.hpp"

namespace datadog::impl {

namespace {

constexpr std::string_view kRumSessionBaggageKey = "session.id";
constexpr std::string_view kAccountBaggageKey = "account.id";
constexpr std::string_view kUserBaggageKey = "user.id";

constexpr std::string_view kBaggageHeaderName = "baggage";

}  // namespace

void BaggageHeaderWriter::AppendHeaders(
    TraceContext& context, std::vector<HttpHeader>& out
) {
  std::string baggage;

  if (context.session_id) {
    baggage.append(kRumSessionBaggageKey);
    baggage.append("=");
    baggage.append(context.session_id->ToString());
  }

  if (const auto& user_id = context.user_id) {
    if (!baggage.empty()) {
      baggage.append(",");
    }
    baggage.append(kUserBaggageKey);
    baggage.append("=");
    AppendEncodedValue(*user_id, baggage);
  }

  if (const auto& account_id = context.account_id) {
    if (!baggage.empty()) {
      baggage.append(",");
    }
    baggage.append(kAccountBaggageKey);
    baggage.append("=");
    AppendEncodedValue(*account_id, baggage);
  }

  if (!baggage.empty()) {
    out.push_back(
        HttpHeaderWriter::CreateHeader(kBaggageHeaderName, std::move(baggage))
    );
  }
}

void BaggageHeaderWriter::AppendEncodedValue(std::string_view value, std::string& out) {
  constexpr std::string_view kHexDigits = "0123456789ABCDEF";

  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte >= 0x21 && byte <= 0x7E && byte != '"' && byte != ',' && byte != ';' &&
        byte != '\\' && byte != '%') {
      out.push_back(character);
    } else {
      out.push_back('%');
      out.push_back(kHexDigits[byte >> 4]);
      out.push_back(kHexDigits[byte & 0x0F]);
    }
  }
}

}  // namespace datadog::impl
