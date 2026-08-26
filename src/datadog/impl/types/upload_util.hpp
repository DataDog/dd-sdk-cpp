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

/**
 * Builds the string value for the core set of 'ddtags' values used to label uploaded
 * data, as a comma-separated string containing `key:value` pairs, with any occurrence
 * of ',' or ':' stripped out of all input values.
 *
 * In this context, 'ddtags' refers to the fixed set of values used for unified service
 * tagging, supplied with uploads in event payloads, typically in a top-level property
 * called 'ddtags' on each event object. (These values are distinct from a separate set
 * of values provided per-HTTP-request in a `?ddtags` query parameter, which shares the
 * same name for historical reasons.)
 *
 * `tail`, if provided, represents an additional set of comma-delimited tag values to be
 * concatenated with the result.
 */
std::string BuildDdTags(
    std::string_view service,
    std::string_view application_version,
    std::string_view env,
    std::string_view sdk_version,
    std::string_view variant,
    std::string_view tail = {}
);

}  // namespace datadog::impl
