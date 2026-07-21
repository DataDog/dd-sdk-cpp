// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/http/request_builder.hpp"

#include <string_view>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/context.hpp"

#include "support/catch.hpp"
#include "support/context.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("HttpRequestBuilder", "[unit][core]") {
  // Given an initial CoreContext value that contains the immutable configuration
  // details affecting HTTP requests (none of which can change after SDK start)
  CoreConfig config("my-client-token", "my-service", "my-env");
  config.Internal_UseCustomEndpoint("https://example.com/intake");
  config.Internal_SetSource("some-sdk");
  config.Internal_SetSdkVersion("4.5.6");
  ImmutableContext imm(
      config, MOCK_OS_INFO, MOCK_DEVICE_INFO, Timestamp{}, "http-client", "1.1.2"
  );
  CoreContext ctx(imm, TrackingConsent::Pending);

  // And a HttpRequestBuilder initialized from that context
  HttpRequestBuilder builder(ctx);

  SECTION("M produce expected URL and headers") {
    // When we build a request to /foo/bar with a couple of parameters and custom
    // headers
    builder.Reset("/foo/bar", "text/plain");
    builder.AddQueryParam("x", "42");
    builder.AddQueryParam("y", "one:beef,two:lamb");
    builder.AddHeader("X-Some-Header", "my cool header");
    builder.AddHeader("Whatever", "Hello");

    // Then the resulting URL uses the configured intake origin and contains our custom
    // query param values
    REQUIRE(
        std::string_view{builder.GetUrl()} ==
        "https://example.com/intake/foo/bar?x=42&y=one:beef,two:lamb"
    );

    // And the resulting set of CRLF-delimited headers contains the Content-Type we used
    std::string headers = builder.GetHeaders();
    using namespace Catch::Matchers;
    REQUIRE_THAT(headers, ContainsSubstring("Content-Type: text/plain\r\n"));

    // And all standard headers are in place and reflect our config and device/OS info
    REQUIRE_THAT(
        headers,
        ContainsSubstring(
            "User-Agent: my-service http-client/1.1.2 (mock-device; mock-os/2.3.4)\r\n"
        )
    );
    REQUIRE_THAT(headers, ContainsSubstring("DD-API-KEY: my-client-token\r\n"));
    REQUIRE_THAT(headers, ContainsSubstring("DD-EVP-ORIGIN: some-sdk\r\n"));
    REQUIRE_THAT(headers, ContainsSubstring("DD-EVP-ORIGIN-VERSION: 4.5.6\r\n"));

    // And a DD-REQUEST-ID header is in place, with a random UUID as its value
    REQUIRE_THAT(headers, ContainsSubstring("DD-REQUEST-ID: "));
    auto request_id_pos = headers.find("DD-REQUEST-ID: ");
    REQUIRE(request_id_pos != std::string_view::npos);
    request_id_pos += std::string_view{"DD-REQUEST-ID: "}.size();
    REQUIRE(request_id_pos + 36 < headers.size());
    auto uuid_opt = UUID::Parse(std::string_view{headers.data() + request_id_pos, 36});
    REQUIRE(uuid_opt.has_value());
    REQUIRE(uuid_opt.value() != UUID::Zero);

    // And our custom headers are in place as well
    REQUIRE_THAT(headers, ContainsSubstring("X-Some-Header: my cool header\r\n"));
    REQUIRE_THAT(headers, ContainsSubstring("Whatever: Hello\r\n"));
  }

  SECTION("M generate new DD-REQUEST-ID W builder is reused for a subsequent request") {
    // When we build a request to /foo/bar
    builder.Reset("/foo/bar", "text/plain");
    builder.AddQueryParam("x", "42");
    builder.AddHeader("X-Some-Header", "hello");

    // And observe the DD-REQUEST-ID value generated for that initial request
    std::string foo_bar_headers = builder.GetHeaders();
    auto foo_bar_request_id_pos = foo_bar_headers.find("DD-REQUEST-ID: ");
    foo_bar_request_id_pos += std::string_view{"DD-REQUEST-ID: "}.size();
    auto foo_bar_request_id_opt = UUID::Parse(
        std::string_view{foo_bar_headers.data() + foo_bar_request_id_pos, 36}
    );
    REQUIRE(foo_bar_request_id_opt.has_value());
    const UUID& foo_bar_request_id = *foo_bar_request_id_opt;
    REQUIRE(foo_bar_request_id != UUID::Zero);

    // And then we build another request to /baz
    builder.Reset("/baz", "application/json");
    builder.AddQueryParam("y", "99");

    // Then our new URL reflects the path and query params for our new request, without
    // preserving any details of the first request
    REQUIRE(
        std::string_view{builder.GetUrl()} == "https://example.com/intake/baz?y=99"
    );

    // And our new set of headers is also faithful to our new request, w/o old values
    std::string baz_headers = builder.GetHeaders();
    REQUIRE_THAT(baz_headers, !Catch::Matchers::ContainsSubstring("X-Some-Header: "));

    // And our new set of headers contains a DD-REQUEST-ID value that's different than
    // the one from our first request
    auto baz_request_id_pos = baz_headers.find("DD-REQUEST-ID: ");
    baz_request_id_pos += std::string_view{"DD-REQUEST-ID: "}.size();
    auto baz_request_id_opt =
        UUID::Parse(std::string_view{baz_headers.data() + baz_request_id_pos, 36});
    REQUIRE(baz_request_id_opt.has_value());
    const UUID& baz_request_id = *baz_request_id_opt;
    REQUIRE(baz_request_id != UUID::Zero);
    REQUIRE(baz_request_id != foo_bar_request_id);
  }

  SECTION("M add ?ddsource=<source> W AddQueryParam_ddsource() is called") {
    // When we build a request to /foo/bar, and include the standard ddsource param
    builder.Reset("/foo/bar", "text/plain");
    builder.AddQueryParam_ddsource();

    // Then our resulting URL contains a 'ddsource' query param whose value is equal to
    // our configured 'source'
    REQUIRE(
        std::string_view{builder.GetUrl()} ==
        "https://example.com/intake/foo/bar?ddsource=some-sdk"
    );
  }
}
