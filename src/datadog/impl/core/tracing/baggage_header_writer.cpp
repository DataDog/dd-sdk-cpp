// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/baggage_header_writer.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "datadog/impl/core/tracing/http_header_factory.hpp"

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
    if (baggage.size() > 0) {
      baggage.append(",");
    }
    baggage.append(kUserBaggageKey);
    baggage.append("=");
    baggage.append(*user_id);
  }

  if (const auto& account_id = context.account_id) {
    if (baggage.size() > 0) {
      baggage.append(",");
    }
    baggage.append(kAccountBaggageKey);
    baggage.append("=");
    baggage.append(*account_id);
  }

  if (baggage.size() > 0) {
    out.push_back(HttpHeaderFactory::Create(kBaggageHeaderName, std::move(baggage)));
  }
}

}  // namespace datadog::impl
