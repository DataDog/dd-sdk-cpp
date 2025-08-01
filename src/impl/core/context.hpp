#pragma once

#include <cinttypes>
#include <string>

#include "datadog/core.hpp"
#include "core/site.hpp"

namespace datadog::impl {

struct CoreContext
{
    uint32_t version;

    std::string intake_origin;
    std::string client_token;
    std::string service;
    std::string env;
    std::string application_version;
    std::string source;

    CoreContext(const datadog::CoreConfig& config)
        : version(0)
        , intake_origin(GetIntakeOrigin(config.datadog_site, ""))
        , client_token(config.client_token)
        , service(config.service)
        , env(config.env)
        , application_version(config.application_version)
        , source("unity") // TODO(RUM-7416): "rum-cpp" is not yet supported as a source
    {
    }

    void SetService(std::string_view value)
    {
        service = value;
        version++;
    }

    void SetEnv(std::string_view value)
    {
        env = value;
        version++;
    }
};

}
