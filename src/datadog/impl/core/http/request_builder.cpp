// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/http/request_builder.hpp"

#include <algorithm>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

HttpRequestBuilder::HttpRequestBuilder(const CoreContext& ctx)
    : _intake_origin(ctx.intake_origin),
      _user_agent(ctx.user_agent),
      _client_token(ctx.client_token),
      _source(ctx.source),
      _sdk_version(ctx.sdk_version) {
  _url_buffer.reserve(256);
  _headers_buffer.reserve(512);
}

HttpRequestBuilder& HttpRequestBuilder::Reset(
    std::string_view path, std::string_view content_type
) {
  // Origin should never end with '/'; path should always start with '/'
  DATADOG_ASSERT(
      !_intake_origin.empty() && _intake_origin.back() != '/',
      "HTTP origin not normalized to strip trailing slash"
  );
  DATADOG_ASSERT(
      !path.empty() && path.front() == '/',
      "feature erroneously supplied URL path without leading slash"
  );

  // Reset all internal state: note that this invalidates any values retrieved via
  // GetUrl() or GetHeaders()
  _query_delim = '?';
  _url_buffer.clear();
  _headers_buffer.clear();

  // Reset our base URL (origin + path), with no query params
  const size_t url_len = _intake_origin.size() + path.size();
  _url_buffer.reserve(url_len);
  _url_buffer += _intake_origin;
  _url_buffer += path;

  // Populate the feature-provided Content-Type, followed by the standard set of headers
  // that are used for all HTTP requests
  AddHeader("Content-Type", content_type);
  AddHeader("User-Agent", _user_agent);
  AddHeader("DD-API-KEY", _client_token);
  AddHeader("DD-EVP-ORIGIN", _source);
  AddHeader("DD-EVP-ORIGIN-VERSION", _sdk_version);

  // Generate a random DD-REQUEST-ID value for every request. If a request fails and is
  // later retried, the upload thred will reinitiate this entire request-building
  // process for the retry, so each new request will naturally have its own ID, even for
  // the same batch of data
  char request_id_buf[36];
  UUID::Random().ToBytes(&request_id_buf[0], sizeof(request_id_buf));
  std::string_view request_id{&request_id_buf[0], sizeof(request_id_buf)};
  AddHeader("DD-REQUEST-ID", request_id);

  return *this;
}

HttpRequestBuilder& HttpRequestBuilder::AddQueryParam(
    std::string_view name, std::string_view value
) {
  // We only use this function internally with values that do not require URL-encoding:
  // if that changes, catch it with an assert
#if WITH_DATADOG_ASSERTS
  auto is_url_safe = [](char c) -> bool {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~' || c == ',' || c == ':';
  };
  DATADOG_ASSERT(
      std::all_of(name.begin(), name.end(), is_url_safe),
      "query parameter name contains characters that may require URL encoding"
  );
  DATADOG_ASSERT(
      std::all_of(value.begin(), value.end(), is_url_safe),
      "query parameter value contains characters that may require URL encoding"
  );
#endif

  // If our URL buffer needs to grow to fit '&<name>=<value>', perform a single
  // preemptive reallocation
  const size_t delim_len = 1;
  const size_t equals_len = 1;
  const size_t extra_len = delim_len + name.size() + equals_len + value.size();
  _url_buffer.reserve(_url_buffer.size() + extra_len);

  // Append '?|&', then '<name>=<value>'
  _url_buffer += _query_delim;
  _url_buffer += name;
  _url_buffer += '=';
  _url_buffer += value;

  // Ensure that our next parameter will be appended with '&' rather than '?'
  _query_delim = '&';

  return *this;
}

HttpRequestBuilder& HttpRequestBuilder::AddQueryParam_ddsource() {
  return AddQueryParam("ddsource", _source);
}

HttpRequestBuilder& HttpRequestBuilder::AddHeader(
    std::string_view name, std::string_view value
) {
  // Header values should not contain CRLF
  DATADOG_ASSERT(
      value.find("\r\n") == std::string_view::npos,
      "value passed to AddHeader contains CRLF"
  );

  // If the buffer containing CRLF-delimited header values needs to grow to fit
  // '<name>: <value>\r\n', perform a single preemptive reallocation
  const size_t colon_space_len = 2;
  const size_t crlf_len = 2;
  const size_t extra_len = name.size() + colon_space_len + value.size() + crlf_len;
  _headers_buffer.reserve(_headers_buffer.size() + extra_len);

  // Append '<name>: <value>\r\n'
  _headers_buffer += name;
  _headers_buffer += ": ";
  _headers_buffer += value;
  _headers_buffer += "\r\n";

  return *this;
}

}  // namespace datadog::impl
