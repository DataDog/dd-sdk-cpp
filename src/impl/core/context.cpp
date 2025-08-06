#include "core/context.hpp"

#include "core/version.hpp"

namespace datadog::impl {

void CoreContext::BuildRequestURL(
    std::string_view path,
    bool with_ddsource,
    std::string& out_url
) const
{
    static const std::string_view ddsource_param = "ddsource=";

    // Origin should never end with '/'; path should always start with '/'
    assert(intake_origin.size() > 0 && intake_origin.back() != '/');
    assert(path.size() > 0 && path.front() == '/');

    // Compute the size of our updated URL
    size_t url_len = intake_origin.size() + path.size();
    if (with_ddsource)
    {
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
    if (with_ddsource)
    {
        out_url += query_delim;
        out_url += ddsource_param;
        out_url += source;
        query_delim = '&';
    }
}

void CoreContext::BuildRequestHeaders(
    std::string_view content_type,
    std::string_view feature_headers,
    std::string& out_headers
) const
{
    // These standard headers are set by this core implementation, for all features
    static const std::string_view dd_api_key = "DD-API-KEY: ";
    static const std::string_view dd_evp_origin = "DD-EVP-ORIGIN: ";
    static const std::string_view dd_evp_origin_version = "DD-EVP-ORIGIN-VERSION: ";
    static const std::string_view dd_request_id = "DD-REQUEST-ID: ";
    static const std::string_view user_agent = "User-Agent: ";

    // If the feature supplies extra headers, they should always end with a newline
    assert(feature_headers.empty() || feature_headers.back() == '\n');

    // Values for standard headers should not be supplied by the feature implementation
    assert(feature_headers.find(dd_api_key) == std::string_view::npos);
    assert(feature_headers.find(dd_evp_origin) == std::string_view::npos);
    assert(feature_headers.find(dd_evp_origin_version) == std::string_view::npos);
    assert(feature_headers.find(dd_request_id) == std::string_view::npos);
    assert(feature_headers.find(user_agent) == std::string_view::npos);

    // TODO: Generate Request ID
    static const size_t HYPHENATED_UUID_LEN = 36;
    static const std::string_view request_id = "00000000-0000-0000-0000-000000000000";
    assert(request_id.size() == HYPHENATED_UUID_LEN);

    // TODO: Generate User-Agent
    static const std::string_view user_agent_value = "nobody";

    // Compute the size of our final set of headers, with values
    const size_t headers_len = (
        dd_api_key.size() + client_token.size() + 1 +
        dd_evp_origin.size() + source.size() + 1 +
        dd_evp_origin_version.size() + SDK_VERSION.size() + 1 +
        dd_request_id.size() + HYPHENATED_UUID_LEN + 1 +
        user_agent.size() + user_agent_value.size() + 1 +
        feature_headers.size()
    );
    
    // Ensure that our destination string has enough memory to fit everything
    out_headers.reserve(headers_len);

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
    out_headers += request_id;
    out_headers += '\n';

    // Concatenate User-Agent
    out_headers += user_agent;
    out_headers += user_agent_value;
    out_headers += '\n';

    // Tack on any feature-supplied headers
    out_headers += feature_headers;
}

}
