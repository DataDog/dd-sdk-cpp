// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/upload_util.hpp"

#include <sstream>

#include "datadog/impl/types/assert.hpp"

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
    case Site::us2_fed:
      return "browser-intake-us2-ddog-gov.com";

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

std::string BuildDdTags(
    std::string_view service,
    std::string_view application_version,
    std::string_view env,
    std::string_view sdk_version,
    std::string_view variant,
    std::string_view tail
) {
  // Prepare prefixes for all supported 'key:value' pairs
  constexpr std::string_view service_prefix = "service:";
  constexpr std::string_view version_prefix = "version:";
  constexpr std::string_view env_prefix = "env:";
  constexpr std::string_view sdk_version_prefix = "sdk_version:";
  constexpr std::string_view variant_prefix = "variant:";

  // service and env are set via config and are required in order to initialize the SDK;
  // sdk_version is resolved internally and will never be empty
  DATADOG_ASSERT(!service.empty(), "attempting to build ddtags w/o service");
  DATADOG_ASSERT(!env.empty(), "attempting to build ddtags w/o env");
  DATADOG_ASSERT(!sdk_version.empty(), "attempting to build ddtags w/o sdk_version");
  size_t max_size = (service_prefix.size() + service.size()) +
                    (1 + env_prefix.size() + env.size()) +
                    (1 + sdk_version_prefix.size() + sdk_version.size());

  // version, variant, and tail are not required; add them to our precomputed max size
  // if present
  if (!application_version.empty()) {
    max_size += 1 + version_prefix.size() + application_version.size();
  }
  if (!variant.empty()) {
    max_size += 1 + variant_prefix.size() + variant.size();
  }
  if (!tail.empty()) {
    max_size += 1 + tail.size();
  }

  // Allocate a string large enough to hold the worst-case ddtags value, assuming no
  // reserved characters need to be filtered out
  std::string result;
  result.reserve(max_size);

  // We control the tag names, but application-provided values used in ddtags must be
  // sanitized to strip any commas or colons
  auto append_sanitized_tag_value = [&result](std::string_view s) {
    for (const char c : s) {
      if (c == ',' || c == ':') {
        // In the edge case where every value in a non-empty string is a colon or comma,
        // we end up with an empty tag value and that's OK
        continue;
      }
      result += c;
    }
  };

  // Append 'service:<service>' (required)
  result += service_prefix;
  append_sanitized_tag_value(service);

  // Append ',version:<version>' if specified
  if (!application_version.empty()) {
    result += ',';
    result += version_prefix;
    append_sanitized_tag_value(application_version);
  }

  // Append ',env:<env>' (required)
  result += ',';
  result += env_prefix;
  append_sanitized_tag_value(env);

  // Append ',sdk_version:<sdk_version>' (required)
  result += ',';
  result += sdk_version_prefix;
  append_sanitized_tag_value(sdk_version);

  // Append ',variant:<variant>' if specified
  if (!variant.empty()) {
    result += ',';
    result += variant_prefix;
    append_sanitized_tag_value(variant);
  }

  // Append ',<tail>' if provided, assuming that the value is already sanitized (see
  // also LoggerTags)
  if (!tail.empty()) {
    result += ',';
    result += tail;
  }

  return result;
}

}  // namespace datadog::impl
