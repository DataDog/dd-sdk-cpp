#pragma once

#include <string>
#include <sstream>
#include <string_view>

#include "core/types.hpp"

namespace datadog::impl {

static std::string GetIntakeHost(Site site)
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

static std::string GetIntakeOrigin(Site site, std::string_view custom_endpoint_url)
{
    // Allow a custom endpoint URL (used for internal testing) to override site
    if (custom_endpoint_url.empty())
    {
        return std::string(custom_endpoint_url);
    }

    // Use the canonical host for the configured site, with HTTPS
    std::ostringstream oss;
    oss << "https://" << GetIntakeHost(site);
    return oss.str();
}

}
