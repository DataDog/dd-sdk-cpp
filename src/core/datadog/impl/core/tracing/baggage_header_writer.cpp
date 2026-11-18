// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/baggage_header_writer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "datadog/impl/core/tracing/http_header_writer.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

namespace {

constexpr std::string_view kRumSessionBaggageKey = "session.id";
constexpr std::string_view kAccountBaggageKey = "account.id";
constexpr std::string_view kUserBaggageKey = "user.id";

constexpr std::string_view kBaggageHeaderName = "baggage";

// Keep the complete HTTP/1.1 header line within 8 KiB, including
// the header name, ": ", and the terminating CRLF.
constexpr std::size_t kMaxBaggageSize = 8192 - kBaggageHeaderName.size() - 2 - 2;

}  // namespace

void BaggageHeaderWriter::AppendHeaders(
    TraceContext& context, const DiagnosticLogger& logger, std::vector<HttpHeader>& out
) {
  std::string baggage;

  if (context.session_id) {
    baggage.append(kRumSessionBaggageKey);
    baggage.append("=");
    baggage.append(context.session_id->ToString());
  }

  if (const auto& user_id = context.user_id) {
    AppendEntryWithLogging(kUserBaggageKey, *user_id, logger, baggage);
  }

  if (const auto& account_id = context.account_id) {
    AppendEntryWithLogging(kAccountBaggageKey, *account_id, logger, baggage);
  }

  if (!baggage.empty()) {
    out.push_back(
        HttpHeaderWriter::CreateHeader(kBaggageHeaderName, std::move(baggage))
    );
  }
}

void BaggageHeaderWriter::AppendEntryWithLogging(
    std::string_view key,
    std::string_view value,
    const DiagnosticLogger& logger,
    std::string& out
) {
  if (!AppendEntry(key, value, out)) {
    logger.Warning(
        "Baggage entry omitted: encoded header would exceed the size limit",
        {{"key", key}, {"max_size", kMaxBaggageSize}}
    );
  }
}

bool BaggageHeaderWriter::AppendEntry(
    std::string_view key, std::string_view value, std::string& out
) {
  // 1. Measure if the key/value pair fits before appending.
  // Start by the "[,]key=" part.
  const std::size_t separator_size = out.empty() ? 0 : 1;
  const std::size_t prefix_size = separator_size + key.size() + 1;
  std::size_t remaining_size = kMaxBaggageSize - out.size();
  if (prefix_size > remaining_size) {
    return false;
  }
  remaining_size -= prefix_size;

  // Shortcut: if the value does not fit even before encoding, bail out.
  if (value.size() > remaining_size) {
    return false;
  }

  // Verify if the encoded value fits.
  for (const char character : value) {
    const std::size_t encoded_size =
        IsUnescapedValueByte(static_cast<unsigned char>(character)) ? 1 : 3;
    if (encoded_size > remaining_size) {
      return false;
    }
    remaining_size -= encoded_size;
  }

  // 2. Append everything.
  if (separator_size != 0) {
    out.push_back(',');
  }
  out.append(key);
  out.push_back('=');
  AppendEncodedValue(value, out);
  return true;
}

bool BaggageHeaderWriter::IsUnescapedValueByte(unsigned char byte) {
  return byte >= 0x21 && byte <= 0x7E && byte != '"' && byte != ',' && byte != ';' &&
         byte != '\\' && byte != '%';
}

void BaggageHeaderWriter::AppendEncodedValue(std::string_view value, std::string& out) {
  constexpr std::string_view kHexDigits = "0123456789ABCDEF";

  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (IsUnescapedValueByte(byte)) {
      out.push_back(character);
    } else {
      out.push_back('%');
      out.push_back(kHexDigits[byte >> 4]);
      out.push_back(kHexDigits[byte & 0x0F]);
    }
  }
}

}  // namespace datadog::impl
