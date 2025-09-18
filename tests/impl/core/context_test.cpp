// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/context.hpp"

#include <catch2/catch_test_macros.hpp>

#include "core/version.hpp"

using namespace datadog::impl;
using namespace datadog;

TEST_CASE("CoreContext constructor", "[unit]") {
  SECTION("M initialize from CoreConfig W valid config provided") {
    // Given an ordinary config
    CoreConfig config("test_token_123", "test_service", "test_env");
    config.SetInitialTrackingConsent(TrackingConsent::Granted);
    config.SetApplicationVersion("1.2.3");

    // When CoreContext is constructed from that config
    CoreContext context(config);

    // Then values are sensible
    REQUIRE(context.version == 1);
    REQUIRE(context.client_token == "test_token_123");
    REQUIRE(context.service == "test_service");
    REQUIRE(context.env == "test_env");
    REQUIRE(context.application_version == "1.2.3");
    REQUIRE(context.source == "unity");  // TODO: Update when cpp source exists
    REQUIRE_FALSE(context.intake_origin.empty());
  }
}

TEST_CASE("CoreContext SetService", "[unit]") {
  SECTION("M update service and increment version W new value provided") {
    // Given a CoreContext at version id 1, with service 'original_service'
    CoreConfig config("token", "original_service", "env");
    config.SetInitialTrackingConsent(TrackingConsent::Granted);
    config.SetApplicationVersion("1.0.0");

    CoreContext context(config);
    int original_version = context.version;
    REQUIRE(original_version == 1);

    // When service is set to 'new_service'
    context.SetService("new_service");

    // Then change takes effect and version id is incremented for change detection
    REQUIRE(context.service == "new_service");
    REQUIRE(context.version == original_version + 1);
  }
}

TEST_CASE("CoreContext SetEnv", "[unit]") {
  SECTION("M update env and increment version W new value provided") {
    // Given a CoreContext at version id 1, with env 'original_env'
    CoreConfig config("token", "service", "original_env");
    config.SetInitialTrackingConsent(TrackingConsent::Granted);
    config.SetApplicationVersion("1.0.0");

    CoreContext context(config);
    int original_version = context.version;
    REQUIRE(original_version == 1);

    // When env is set to 'new_env'
    context.SetEnv("new_env");

    // Then change takes effect and version id is incremented for change detection
    REQUIRE(context.env == "new_env");
    REQUIRE(context.version == original_version + 1);
  }
}

TEST_CASE("CoreContext BuildRequestURL", "[unit]") {
  // Given an ordinary config
  CoreConfig config("token", "test_service", "test_env");
  config.SetInitialTrackingConsent(TrackingConsent::Granted);
  config.SetApplicationVersion("1.0.0");
  CoreContext context(config);
  std::string result_url;

  SECTION("M concatenate origin and path W path has no query parameters") {
    context.BuildRequestURL("/api/v1/logs", false, result_url);
    REQUIRE(result_url == "https://browser-intake-datadoghq.com/api/v1/logs");
  }

  SECTION("M append ddsource parameter W with_ddsource is true and path has no query") {
    context.BuildRequestURL("/api/v1/logs", true, result_url);
    // TODO: Update when cpp source exists
    REQUIRE(
        result_url == "https://browser-intake-datadoghq.com/api/v1/logs?ddsource=unity"
    );
  }

  SECTION(
      "M append ddsource with ampersand W with_ddsource is true and path has query"
  ) {
    context.BuildRequestURL("/api/v1/logs?existing=param", true, result_url);
    // TODO: Update when cpp source exists
    REQUIRE(
        result_url ==
        "https://browser-intake-datadoghq.com/api/v1/"
        "logs?existing=param&ddsource=unity"
    );
  }

  SECTION("M not append ddsource W with_ddsource is false") {
    context.BuildRequestURL("/api/v1/logs?existing=param", false, result_url);
    REQUIRE(
        result_url == "https://browser-intake-datadoghq.com/api/v1/logs?existing=param"
    );
  }

  SECTION("M reuse string memory W out_url is reused") {
    // When first call uses a longer URL
    context.BuildRequestURL("/api/v1/logs", true, result_url);
    size_t first_capacity = result_url.capacity();

    // And second call uses a shorter URL
    context.BuildRequestURL("/short", false, result_url);

    // Then no second allocation should occur
    REQUIRE(result_url == "https://browser-intake-datadoghq.com/short");
    REQUIRE(result_url.capacity() >= first_capacity);  // Memory reused
  }
}

TEST_CASE("CoreContext BuildRequestHeaders", "[unit]") {
  // Given an ordinary config
  CoreConfig config("test_client_token_456", "test_service", "production");
  config.SetInitialTrackingConsent(TrackingConsent::Granted);
  config.SetApplicationVersion("2.1.0");
  CoreContext context(config);
  std::string result_headers;

  SECTION("M include standard headers W no feature headers provided") {
    // When
    context.BuildRequestHeaders("application/json; charset=utf-8", "", result_headers);

    // Then expected headers are present
    REQUIRE(
        result_headers.find("Content-Type: application/json; charset=utf-8\n") !=
        std::string::npos
    );
    REQUIRE(
        result_headers.find("DD-API-KEY: test_client_token_456\n") != std::string::npos
    );
    // TODO: Update when cpp source exists
    REQUIRE(result_headers.find("DD-EVP-ORIGIN: unity\n") != std::string::npos);
    // TODO: Update when real request ID generation is implemented
    REQUIRE(
        result_headers.find("DD-REQUEST-ID: 00000000-0000-0000-0000-000000000000\n") !=
        std::string::npos
    );
    // TODO: Update when real User-Agent generation is implemented
    REQUIRE(result_headers.find("User-Agent: nobody\n") != std::string::npos);

    // And DD-EVP-ORIGIN-VERSION matches SDK_VERSION stamped into library build
    std::string expected_version_header = "DD-EVP-ORIGIN-VERSION: ";
    expected_version_header += SDK_VERSION;
    expected_version_header += "\n";
    REQUIRE(result_headers.find(expected_version_header) != std::string::npos);

    REQUIRE(result_headers.back() == '\n');  // Should end with newline
  }

  SECTION("M append feature headers W feature headers provided") {
    // When feature implementation provides custom headers
    std::string feature_headers =
        "X-Custom-Header: custom_value\nX-Another-Header: another_value\n";
    context.BuildRequestHeaders("application/json", feature_headers, result_headers);

    // Then standard headers appear first
    size_t api_key_pos = result_headers.find("DD-API-KEY: test_client_token_456\n");
    REQUIRE(api_key_pos != std::string::npos);

    // And those custom headers are appended unmodified
    size_t custom_header_pos = result_headers.find("X-Custom-Header: custom_value\n");
    REQUIRE(custom_header_pos != std::string::npos);
    REQUIRE(custom_header_pos > api_key_pos);
    size_t another_header_pos =
        result_headers.find("X-Another-Header: another_value\n");
    REQUIRE(another_header_pos != std::string::npos);
    REQUIRE(another_header_pos > custom_header_pos);

    REQUIRE(result_headers.back() == '\n');
  }

  SECTION("M reuse string memory W out_headers is reused") {
    // When the first call uses a long set of headers
    std::string long_feature_headers =
        "X-Very-Long-Header-Name: very_long_header_value_that_takes_up_space\n";
    context.BuildRequestHeaders(
        "application/json", long_feature_headers, result_headers
    );
    size_t first_capacity = result_headers.capacity();

    // And the second call uses a shorter set
    context.BuildRequestHeaders("text/plain", "", result_headers);

    // Then no second allocation should occur
    REQUIRE(result_headers.capacity() >= first_capacity);  // Memory reused
    REQUIRE(result_headers.back() == '\n');
  }
}
