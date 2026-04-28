// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <string_view>

namespace datadog::platform {
struct OsInfo;
struct DeviceInfo;
}  // namespace datadog::platform

namespace datadog::impl {

struct CoreContext;

/**
 * Interface used by a Feature to build HTTP requests.
 */
class RequestBuilder {
 public:
  explicit RequestBuilder(const CoreContext& ctx);

  /**
   * Reinitializes the URL and Headers to begin building the details for a new request.
   *
   * `path` must be an HTTP pathname, beginning with '/' - it will be appended to the
   * configured HTTP origin to form the initial URL.
   *
   * `content_type` is the value to be used for the 'Content-Type' header, e.g.
   * 'text/plain' or 'application/json'.
   *
   * Reset() applies the standard set of query params and request headers that the SDK
   * uses in all requests. After calling Reset(), you may set additional query params
   * and header values with AddQueryParam() and AddHeader().
   */
  RequestBuilder& Reset(std::string_view path, std::string_view content_type);

  /**
   * Appends a query parameter to the URL as "?<name>=<value>" or "&<name>=<value>".
   *
   * Note that query parameters are NOT properly URL-encoded: values are assumed to be
   * safe to use as-is in a URL; they will not be percent-escaped. Query parameters are
   * only used internally, in a limited set of cases, e.g.
   * `?source=dd-sdk-cpp&ddtags=retry_count:1,last_failure_status:500`.
   *
   * If any feature passes a non-URL-safe string as a query param (including any string
   * based on application-provided values), this function will need to be updated to
   * escape strings.
   */
  RequestBuilder& AddQueryParam(std::string_view name, std::string_view value);

  /**
   * Calls AddQueryParam("ddsource", <source>), where `source` is the source value
   * configured for this SDK instance (e.g. "rum-cpp", "unity", etc.)
   */
  RequestBuilder& AddQueryParam_ddsource();

  /**
   * Appends a request header value as "<name>: <value>\r\n".
   */
  RequestBuilder& AddHeader(std::string_view name, std::string_view value);

  /**
   * Returns the URL value constructed by the previous call to Reset() and any
   * subsequent AddQueryParam() calls, as a null-terminated C string.
   *
   * The resulting string value is valid only until the next mutation via Reset(),
   * AddQueryParam(), etc.
   */
  const char* GetUrl() const { return _url_buffer.c_str(); }

  /**
   * Returns a null-terminated C string containing all header values constructed by the
   * previous call to Reset() and any subsequent AddHeader() calls. Value is in wire
   * format, with CRLF ("\r\n") separating each "Name: Value" entry, ending with a
   * trailing CRLF.
   *
   * The resulting string value is valid only until the next mutation via Reset(),
   * AddHeader(), etc.
   */
  const char* GetHeaders() const { return _headers_buffer.c_str(); }

 private:
  // Immutable SDK config details pulled from initial CoreContext value: these values
  // are views of std::string values held by the Core, assumed to never change during
  // that Core's lifetime
  std::string_view _intake_origin;  // HTTP origin based on site or custom endpoint
  std::string_view _user_agent;     // Used for User-Agent; encodes app/os/device info
  std::string_view _client_token;   // Used for DD-API-KEY
  std::string_view _source;         // Used for DD-EVP-ORIGIN
  std::string_view _sdk_version;    // Used for DD-EVP-ORIGIN-VERSION

  char _query_delim{'?'};       // Prefix used to append next query param
  std::string _url_buffer;      // URL string to which query params will be appended
  std::string _headers_buffer;  // CRLF-delimited header values
};

}  // namespace datadog::impl
