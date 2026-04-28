// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/upload_util.hpp"

#include <sstream>

namespace datadog::impl {

static bool _is_valid_custom_endpoint_url(std::string_view s) {
  if (s.find("http:") == 0 || s.find("https:") == 0) {
    return s.back() != '/';
  }
  return false;
}

static std::string _replace_whitespace_with_hyphen(std::string_view s) {
  std::string result;
  result.reserve(s.size());
  bool in_whitespace = false;
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!in_whitespace) {
        result += '-';
        in_whitespace = true;
      }
    } else {
      result += c;
      in_whitespace = false;
    }
  }
  return result;
}

std::string GetIntakeHost(Site site) {
  // Use hardcoded values for sites with special URLs
  switch (site) {
    case Site::us1:
      return "browser-intake-datadoghq.com";
    case Site::eu1:
      return "browser-intake-datadoghq.eu";
    case Site::us1_fed:
      return "browser-intake-ddog-gov.com";

    // Fall out to default implementation
    default:
      break;
  }

  // For all other sites, fall back to 'browser-intake-%s-datadoghq.com'
  std::ostringstream oss;
  oss << "browser-intake-" << Site_ToString(site) << "-datadoghq.com";
  return oss.str();
}

std::string GetIntakeOrigin(Site site, std::string_view custom_endpoint_url) {
  // Allow a custom endpoint URL (used for internal testing) to override site
  if (_is_valid_custom_endpoint_url(custom_endpoint_url)) {
    return std::string(custom_endpoint_url);
  }

  // Use the canonical host for the configured site, with HTTPS
  std::ostringstream oss;
  oss << "https://" << GetIntakeHost(site);
  return oss.str();
}

std::string GetUserAgent(
    std::string_view service,
    std::string_view application_version,
    std::string_view reporter_name,
    std::string_view reporter_version,
    std::string_view device_name,
    std::string_view os_name,
    std::string_view os_version
) {
  std::ostringstream oss;

  // First whitespace-delimited token: user application, e.g. 'my-service/1.0.0'
  oss << _replace_whitespace_with_hyphen(service);
  if (!application_version.empty()) {
    oss << '/' << _replace_whitespace_with_hyphen(application_version);
  }
  oss << ' ';

  // Second token: HTTP client implementation, e.g. 'libcurl/8.11.0'
  oss << _replace_whitespace_with_hyphen(reporter_name);
  if (!reporter_version.empty()) {
    oss << '/' << _replace_whitespace_with_hyphen(reporter_version);
  }
  oss << ' ';

  // In parentheses: device and OS, e.g. '(ThinkPad-T14-Gen-2; Ubuntu/22.04)'
  oss << '(' << _replace_whitespace_with_hyphen(device_name);
  oss << "; " << _replace_whitespace_with_hyphen(os_name);
  if (!os_version.empty()) {
    oss << '/' << _replace_whitespace_with_hyphen(os_version);
  }
  oss << ')';

  return oss.str();
}

}  // namespace datadog::impl
