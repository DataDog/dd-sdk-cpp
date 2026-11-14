// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string_view>

#include "datadog/network_instrumentation.hpp"

namespace datadog::impl {

class FirstPartyHostsMatcher {
 public:
  /**
   * Returns the header types, if any, a request to `host` should use for trace
   * propagation.
   *
   * @see @ref datadog::FirstPartyHosts for a description of the matching rules.
   *
   * @param first_party_hosts The configured first party hosts.
   * @param host The host the caller wants to obtain the tracing header types for. Do
   * not include the URL schema (https://, etc) nor a port.
   *
   * @returns A TracingHeaderTypes value with the header types for this host, or
   * `std::nullopt` if the given host does not match any configured host.
   */
  static std::optional<TracingHeaderTypes> MatchesHost(
      const FirstPartyHosts& first_party_hosts, std::string_view host
  );

  /**
   * `true` if `host` matches the `configured_host`, `false` otherwise.
   *
   * @see @ref datadog::FirstPartyHosts for a description of the matching rules.
   *
   * @param configured_host The configured host.
   * @param host The host to be tested against `configured_host`. Do not include
   * the URL schema (https://, etc) nor a port.
   *
   * @returns `true` if `host` is equal to `configured_host`, or a subdomain, `false`
   * otherwise.
   */
  static bool MatchesHost(std::string_view configured_host, std::string_view host);
};

}  // namespace datadog::impl
