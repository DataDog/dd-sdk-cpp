// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "datadog/network_instrumentation.hpp"

#include "datadog/impl/core/tracing/first_party_hosts_matcher.hpp"

using datadog::FirstPartyHost;
using datadog::FirstPartyHosts;
using datadog::TracingHeaderTypes;
using namespace datadog::impl;

TEST_CASE("FirstPartyHosts matching", "[unit][tracing]") {
  SECTION("FirstPartyHostsMatcher::MatchesHost single host") {
    SECTION("M match the configured host") {
      REQUIRE(FirstPartyHostsMatcher::MatchesHost("example.com", "example.com"));
    }

    SECTION("M match a subdomain") {
      REQUIRE(FirstPartyHostsMatcher::MatchesHost("example.com", "api.example.com"));
    }

    SECTION("M match ASCII characters case-insensitively") {
      REQUIRE(FirstPartyHostsMatcher::MatchesHost("Example.COM", "API.eXAMPLE.com"));
    }

    SECTION("M not match a suffix without a domain separator") {
      REQUIRE_FALSE(
          FirstPartyHostsMatcher::MatchesHost("example.com", "notexample.com")
      );
      REQUIRE_FALSE(
          FirstPartyHostsMatcher::MatchesHost("notexample.com", "example.com")
      );
    }

    SECTION("M not match a shorter host") {
      REQUIRE_FALSE(
          FirstPartyHostsMatcher::MatchesHost("api.example.com", "example.com")
      );
    }

    SECTION("M not match a different host") {
      REQUIRE_FALSE(FirstPartyHostsMatcher::MatchesHost("example.com", "example.org"));
    }

    SECTION("M not match an empty configured host") {
      REQUIRE_FALSE(FirstPartyHostsMatcher::MatchesHost("", "example.com"));
    }

    SECTION("M not match an empty host") {
      REQUIRE_FALSE(FirstPartyHostsMatcher::MatchesHost("example.com", ""));
    }
  }

  SECTION("FirstPartyHostsMatcher::MatchesHost FirstPartyHosts") {
    SECTION("M assign default headers W constructed from host strings") {
      const std::vector<std::string> hosts{
          "api.example.com",
          "shop.example.com",
          "cdn.example.com",
      };
      const FirstPartyHosts first_party_hosts{hosts};

      for (const auto& host : hosts) {
        CAPTURE(host);
        REQUIRE(
            FirstPartyHostsMatcher::MatchesHost(first_party_hosts, host) ==
            TracingHeaderTypes::Default
        );
      }
    }

    SECTION("M match an exact configured host") {
      const FirstPartyHosts hosts{{
          {"api.example.com", TracingHeaderTypes::Datadog},
          {"shop.example.com", TracingHeaderTypes::B3},
          {"cdn.example.com",
           TracingHeaderTypes::Datadog | TracingHeaderTypes::Tracecontext},
      }};

      REQUIRE(
          FirstPartyHostsMatcher::MatchesHost(hosts, "shop.example.com") ==
          datadog::TracingHeaderTypes::B3
      );

      REQUIRE(
          FirstPartyHostsMatcher::MatchesHost(hosts, "cdn.example.com") ==
          (TracingHeaderTypes::Datadog | TracingHeaderTypes::Tracecontext)
      );
    }

    SECTION("M return the first match W multiple configured hosts match") {
      const FirstPartyHosts hosts{{
          {"example.com", TracingHeaderTypes::Datadog},
          {"api.example.com", TracingHeaderTypes::B3},
      }};

      REQUIRE(
          FirstPartyHostsMatcher::MatchesHost(hosts, "api.example.com") ==
          TracingHeaderTypes::Datadog
      );
    }

    SECTION("M match the more specific host W it is configured first") {
      const FirstPartyHosts hosts{{
          {"api.example.com", TracingHeaderTypes::B3},
          {"example.com", TracingHeaderTypes::Datadog},
      }};

      REQUIRE(
          FirstPartyHostsMatcher::MatchesHost(hosts, "api.example.com") ==
          TracingHeaderTypes::B3
      );
      REQUIRE(
          FirstPartyHostsMatcher::MatchesHost(hosts, "shop.example.com") ==
          TracingHeaderTypes::Datadog
      );
    }

    SECTION("M not match W no configured host matches") {
      const FirstPartyHosts hosts{{
          {"example.com", TracingHeaderTypes::Datadog},
          {"example.org", TracingHeaderTypes::B3},
      }};

      REQUIRE_FALSE(
          FirstPartyHostsMatcher::MatchesHost(hosts, "example.net").has_value()
      );
    }

    SECTION("M not match W configured hosts are empty") {
      const FirstPartyHosts hosts{std::vector<FirstPartyHost>{}};

      REQUIRE_FALSE(
          FirstPartyHostsMatcher::MatchesHost(hosts, "example.com").has_value()
      );
    }
  }
}
