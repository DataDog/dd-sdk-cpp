// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/context.hpp"

#include "assert.hpp"
#include "core/version.hpp"
#include "datadog/uuid.hpp"

namespace datadog::impl {

void CoreContext::BuildRequestURL(
    std::string_view path, bool with_ddsource, std::string& out_url
) const {
  static const std::string_view ddsource_param = "ddsource=";

  // Origin should never end with '/'; path should always start with '/'
  DATADOG_ASSERT(
      !intake_origin.empty() && intake_origin.back() != '/',
      "HTTP origin not normalized to strip trailing slash"
  );
  DATADOG_ASSERT(
      !path.empty() && path.front() == '/',
      "feature erroneously supplied URL path without leading slash"
  );

  // Compute the size of our updated URL
  size_t url_len = intake_origin.size() + path.size();
  if (with_ddsource) {
    url_len += 1 + ddsource_param.size() + source.size();
  }

  // Ensure that our destination string has enough memory to fit the new URL
  out_url.reserve(url_len);

  // Replace the string contents
  out_url.clear();
  out_url += intake_origin;
  out_url += path;

  // Append additional query parameters
  char query_delim = path.find('?') == std::string_view::npos ? '?' : '&';
  if (with_ddsource) {
    out_url += query_delim;
    out_url += ddsource_param;
    out_url += source;
    query_delim = '&';  // NOLINT: Defensive; needed if we add ddtags etc.
  }
}

void CoreContext::BuildRequestHeaders(
    std::string_view content_type,
    std::string_view feature_headers,
    std::string& out_headers
) const {
  // These standard headers are set by this core implementation, for all features
  static const std::string_view content_type_header = "Content-Type: ";
  static const std::string_view dd_api_key = "DD-API-KEY: ";
  static const std::string_view dd_evp_origin = "DD-EVP-ORIGIN: ";
  static const std::string_view dd_evp_origin_version = "DD-EVP-ORIGIN-VERSION: ";
  static const std::string_view dd_request_id = "DD-REQUEST-ID: ";
  static const std::string_view user_agent_header = "User-Agent: ";

  // If the feature supplies extra headers, they should always end with a newline
  DATADOG_ASSERT(
      feature_headers.empty() || feature_headers.back() == '\n',
      "feature-supplied headers must be empty or end with a newline"
  );

  // Values for standard headers should not be supplied by the feature implementation
  DATADOG_ASSERT(
      feature_headers.find(content_type_header) == std::string_view::npos,
      "feature erroneously supplies Content-Type header"
  );
  DATADOG_ASSERT(
      feature_headers.find(dd_api_key) == std::string_view::npos,
      "feature erroneously supplies DD-API-KEY header"
  );
  DATADOG_ASSERT(
      feature_headers.find(dd_evp_origin) == std::string_view::npos,
      "feature erroneously supplies DD-EVP-ORIGIN header"
  );
  DATADOG_ASSERT(
      feature_headers.find(dd_evp_origin_version) == std::string_view::npos,
      "feature erroneously supplies DD-EVP-ORIGIN-VERSION header"
  );
  DATADOG_ASSERT(
      feature_headers.find(dd_request_id) == std::string_view::npos,
      "feature erroneously supplies DD-REQUEST-ID header"
  );
  DATADOG_ASSERT(
      feature_headers.find(user_agent_header) == std::string_view::npos,
      "feature erroneously supplies User-Agent header"
  );

  // Generate a random UUID to identify this request
  static const size_t HYPHENATED_UUID_LEN = 36;
  const UUID request_id = UUID::Random();

  // TODO: Generate User-Agent
  static const std::string_view user_agent = "nobody";

  // Compute the size of our final set of headers, with values
  const size_t headers_len =
      (content_type_header.size() + content_type.size() + 1) +
      (dd_api_key.size() + client_token.size() + 1) +
      (dd_evp_origin.size() + source.size() + 1) +
      (dd_evp_origin_version.size() + SDK_VERSION.size() + 1) +
      (dd_request_id.size() + HYPHENATED_UUID_LEN + 1) +
      (user_agent_header.size() + user_agent.size() + 1 + feature_headers.size());

  // Ensure that our destination string has enough memory to fit everything
  out_headers.reserve(headers_len);

  // Concatenate Content-Type
  out_headers += content_type_header;
  out_headers += content_type;
  out_headers += '\n';

  // Concatenate DD-API-KEY
  out_headers += dd_api_key;
  out_headers += client_token;
  out_headers += '\n';

  // Concatenate DD-EVP-ORIGIN
  out_headers += dd_evp_origin;
  out_headers += source;
  out_headers += '\n';

  // Concatenate DD-EVP-ORIGIN-VERSION
  out_headers += dd_evp_origin_version;
  out_headers += SDK_VERSION;
  out_headers += '\n';

  // Concatenate DD-REQUEST-ID
  out_headers += dd_request_id;
  const size_t uuid_start = out_headers.size();
  out_headers.resize(uuid_start + HYPHENATED_UUID_LEN);
  request_id.ToBytes(
      out_headers.data() + uuid_start, out_headers.capacity() - uuid_start
  );
  out_headers += '\n';

  // Concatenate User-Agent
  out_headers += user_agent_header;
  out_headers += user_agent;
  out_headers += '\n';

  // Tack on any feature-supplied headers
  out_headers += feature_headers;
}

}  // namespace datadog::impl
