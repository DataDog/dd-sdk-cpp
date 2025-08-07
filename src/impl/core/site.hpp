#pragma once

#include <sstream>
#include <string>
#include <string_view>

#include "core/types.hpp"

namespace datadog::impl {

static bool _is_valid_custom_endpoint_url(std::string_view s)
{
    return s.find("http:") == 0 || s.find("https:") == 0;
}

/**
 * Returns the host (i.e. hostname and port, if any) associated with the intake server
 * for the given Datadog site (i.e. datacenter).
 */
inline std::string GetIntakeHost(Site site)
{
    // Use hardcoded values for sites with special URLs
    switch (site)
    {
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

/**
 * Given the relevant configuration options, returns the origin (i.e. protocol and host)
 */
inline std::string GetIntakeOrigin(Site site, std::string_view custom_endpoint_url)
{
    // Allow a custom endpoint URL (used for internal testing) to override site
    if (_is_valid_custom_endpoint_url(custom_endpoint_url))
    {
        return std::string(custom_endpoint_url);
    }

    // Use the canonical host for the configured site, with HTTPS
    std::ostringstream oss;
    oss << "https://" << GetIntakeHost(site);
    return oss.str();
}

}
