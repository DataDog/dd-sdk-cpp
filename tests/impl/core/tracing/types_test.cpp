// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <iterator>

#include "datadog/network_instrumentation.h"
#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/core/tracing/first_party_hosts_matcher.hpp"

using datadog::FirstPartyHosts_FromC;
using datadog::TracingHeaderTypes;
using datadog::impl::FirstPartyHostsMatcher;

TEST_CASE("FirstPartyHosts_FromC", "[unit][tracing]") {
  SECTION("M convert first-party hosts") {
    const auto combined = static_cast<dd_tracing_header_types_t>(
        DD_TRACING_HEADER_TYPES_B3 | DD_TRACING_HEADER_TYPES_TRACE_CONTEXT
    );
    const dd_first_party_host_t entries[] = {
        {"example.com", combined},
        {"example.org", DD_TRACING_HEADER_TYPES_DATADOG},
    };
    const dd_first_party_hosts_t value{entries, std::size(entries)};

    const auto hosts = FirstPartyHosts_FromC(value);

    REQUIRE(
        FirstPartyHostsMatcher::MatchesHost(hosts, "api.example.com") ==
        (TracingHeaderTypes::B3 | TracingHeaderTypes::Tracecontext)
    );
    REQUIRE(
        FirstPartyHostsMatcher::MatchesHost(hosts, "example.org") ==
        TracingHeaderTypes::Datadog
    );
    REQUIRE_FALSE(FirstPartyHostsMatcher::MatchesHost(hosts, "example.net"));
  }

  SECTION("M copy host strings") {
    char host[] = "example.com";
    const dd_first_party_host_t entries[] = {
        {host, DD_TRACING_HEADER_TYPES_DATADOG},
    };
    const dd_first_party_hosts_t value{entries, std::size(entries)};

    const auto hosts = FirstPartyHosts_FromC(value);
    host[0] = 'x';

    REQUIRE(
        FirstPartyHostsMatcher::MatchesHost(hosts, "example.com") ==
        TracingHeaderTypes::Datadog
    );
    REQUIRE_FALSE(FirstPartyHostsMatcher::MatchesHost(hosts, "xample.com"));
  }

  SECTION("M convert an empty host collection") {
    const auto hosts = FirstPartyHosts_FromC({nullptr, 0});

    REQUIRE_FALSE(FirstPartyHostsMatcher::MatchesHost(hosts, "example.com"));
  }
}
