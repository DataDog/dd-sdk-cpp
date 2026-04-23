// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <string_view>

#include "datadog/impl/core/types.hpp"

namespace datadog::impl {

/**
 * Returns the host (i.e. hostname and port, if any) associated with the intake server
 * for the given Datadog site (i.e. datacenter).
 */
std::string GetIntakeHost(Site site);

/**
 * Given the relevant configuration options, returns the origin (i.e. protocol and host)
 * to be used as the basis of all URLs for requests that upload SDK-generated event data
 * to Datadog intake.
 */
std::string GetIntakeOrigin(Site site, std::string_view custom_endpoint_url);

/**
 * Builds the string value used for the User-Agent header in all HTTP requests made by
 * the SDK.
 */
std::string GetUserAgent(
    std::string_view service,
    std::string_view application_version,
    std::string_view reporter_name,
    std::string_view reporter_version,
    std::string_view device_name,
    std::string_view os_name,
    std::string_view os_version
);

}  // namespace datadog::impl
