#pragma once

#include <string>

#include "core/site.hpp"
#include "datadog/core.hpp"

namespace datadog::impl {

/**
 * Details of SDK configuration and/or state that influence how HTTP requests are built.
 *
 * Maintained by the Core; provided to Features in UploadThread_PrepareReport.
 */
struct CoreContext {
  int version{1};

  std::string intake_origin;
  std::string client_token;
  std::string service;
  std::string env;
  std::string application_version;
  std::string source;

  /**
   * Initializes a new context from the user-supplied config.
   */
  explicit CoreContext(const datadog::CoreConfig& config)
      : intake_origin(GetIntakeOrigin(config.datadog_site, config.custom_endpoint_url)),
        client_token(config.client_token),
        service(config.service),
        env(config.env),
        application_version(config.application_version),
        source("unity")  // TODO(RUM-7416): "rum-cpp" is not yet supported as a source
  {}

  void SetService(std::string_view value) {
    // TODO: This change is not pushed to the upload thread
    service = value;
    version++;
  }

  void SetEnv(std::string_view value) {
    // TODO: This change is not pushed to the upload thread
    env = value;
    version++;
  }

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
      std::string_view content_type, std::string_view feature_headers,
      std::string& out_headers
  ) const;
};

}  // namespace datadog::impl
