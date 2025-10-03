// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>

#include "core/site.hpp"
#include "datadog/core.hpp"

namespace datadog::impl {

/**
 * SDK configuration details that influence how HTTP requests are built. Immutable for
 * the lifetime of the Core.
 */
struct HttpContext {
  /**
   * HTTP origin describing the intake endpoint where requests should be sent.
   */
  std::string intake_origin;
  /**
   * Client token used to authorize HTTP requests sent to intake.
   */
  std::string client_token;
  /**
   * The 'service' value from SDK configuration.
   */
  std::string service;
  /**
   * The 'env' value from SDK configuration.
   */
  std::string env;
  /**
   * The 'version' value from SDK configuration.
   */
  std::string application_version;
  /**
   * The 'source' value indicating the Datadog SDK product in use: 'rum-cpp' for native
   * C++ applications; or another value if used by a multi-platform SDK.
   *
   * TODO(RUM-7416): Currently hardcoded to 'unity' due to lack of backend support for a
   * cpp-specific 'source' type.
   */
  std::string source;

  /**
   * Initializes a new context from the user-supplied config.
   */
  explicit HttpContext(const CoreConfig& config);

  // HttpContext is noncopyable and nonmovable
  HttpContext(const HttpContext&) = delete;
  HttpContext& operator=(const HttpContext&) = delete;
  HttpContext(HttpContext&&) = delete;
  HttpContext& operator=(HttpContext&&) = delete;

  /**
   * Given the basic details of a request, populates out_url with the fully qualified
   * URL used to make that request.
   *
   * @param path URL pathname component, optionally with query params appended after
   *  '?'.
   * @param with_ddsource If true, '(?|&)ddsource=<source>'  will be appended to the
   *  resulting URL.
   * @param out_url Mutable reference to the std::string that will contain the final URL
   *  value. Reuses existing string memory; reallocating only when the string does not
   *  yet have the required capacity.
   */
  void BuildRequestURL(
      std::string_view path, bool with_ddsource, std::string& out_url
  ) const;

  /**
   * Given the basic details of a request, populates out_headers with the full set of
   * headers that can be supplied to an HTTP client to make the request.
   *
   * @param content_type Value to use for the 'Content-Type' header, e.g.
   *  'application/json'.
   * @param feature_headers Additional feature-specific header values to be appended
   *  after the standard headers. If non-empty, must be in wire format with a trailing
   *  newline, e.g. 'X-Some-Header: value\n'.
   * @param out_headers Mutable reference to the std::string that will be reused to
   *  contain the final string. Guaranteed to be non-empty, in wire format, with a
   *  trailing newline.
   */
  void BuildRequestHeaders(
      std::string_view content_type,
      std::string_view feature_headers,
      std::string& out_headers
  ) const;
};

/**
 * Global context shared across the entire SDK instance.
 */
struct CoreContext {
  /**
   * Initializes a new CoreContext from the provided SDK config.
   */
  explicit CoreContext(const CoreConfig& config);

  /**
   * Immutable configuration details that affect the building of HTTP requests in the
   * upload thread. Maintained via std::shared_ptr to ensure that it's cheap to create a
   * copy of CoreContext, since we create an immutable snapshot every time a
   * feature-specific operation depends on CoreContext - @see CoreContextProvider.
   */
  std::shared_ptr<const HttpContext> http;
};

/**
 * Owns a CoreContext and provides thread-safe access to that value.
 */
class CoreContextProvider {
 private:
  CoreContext _context;
  mutable std::shared_mutex _mutex;

 public:
  explicit CoreContextProvider(const CoreContext& context);

  /**
   * Returns an immutable, thread-safe copy of the current CoreContext value.
   */
  CoreContext Get() const;

  /**
   * Mutates the current CoreContext value, by invoking the provided callback after
   * obtaining exclusive write access.
   */
  void Update(const std::function<void(CoreContext&)>& callback);

  /**
   * Synchronously returns an immutable reference to the HttpContext value.
   */
  const HttpContext& GetHttpContext() const;
};

}  // namespace datadog::impl
