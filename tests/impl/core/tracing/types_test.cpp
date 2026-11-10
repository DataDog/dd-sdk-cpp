// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/tracing/types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

#include "datadog/network_instrumentation.h"
#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/core/tracing/first_party_hosts_matcher.hpp"
#include "datadog/impl/core/tracing/http_header_factory.hpp"

using datadog::FirstPartyHosts_FromC;
using datadog::HttpHeader;
using datadog::TracingHeaderTypes;
using datadog::impl::FirstPartyHostsMatcher;
using datadog::impl::HttpHeaderFactory;

static_assert(!std::is_default_constructible_v<HttpHeader>);
static_assert(!std::is_constructible_v<HttpHeader, std::string_view, std::string>);
static_assert(std::is_copy_constructible_v<HttpHeader>);
static_assert(std::is_move_constructible_v<HttpHeader>);

TEST_CASE("HttpHeader", "[unit][tracing]") {
  const auto header = HttpHeaderFactory::Create("header-name", "header-value");

  REQUIRE(header.GetName() == "header-name");
  REQUIRE(header.GetValue() == "header-value");
}

TEST_CASE("FirstPartyHosts_FromC", "[unit][tracing]") {
  SECTION("M convert first-party hosts") {
    const dd_first_party_host_t entries[] = {
        {"example.com",
         DD_TRACING_HEADER_TYPES_B3 | DD_TRACING_HEADER_TYPES_TRACE_CONTEXT},
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
