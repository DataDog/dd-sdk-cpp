// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/context.hpp"

#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "datadog/uuid.hpp"

#include "datadog/impl/core/version.hpp"
#include "datadog/impl/platform/system_info.hpp"

using namespace datadog::impl;
using namespace datadog;

TEST_CASE("HttpContext constructor", "[unit]") {
  SECTION("M initialize from CoreConfig W valid config provided") {
    // Given an ordinary config
    CoreConfig config("test_token_123", "test_service", "test_env");
    config.SetInitialTrackingConsent(TrackingConsent::Granted);
    config.SetApplicationVersion("1.2.3");

    // When HttpContext is constructed from that config
    HttpContext context(config);

    // Then values are sensible
    REQUIRE(context.client_token == "test_token_123");
    REQUIRE(context.service == "test_service");
    REQUIRE(context.env == "test_env");
    REQUIRE(context.application_version == "1.2.3");
    REQUIRE(context.source == "rum-cpp");
    REQUIRE_FALSE(context.intake_origin.empty());
  }
}

TEST_CASE("HttpContext BuildRequestURL", "[unit]") {
  // Given an ordinary config
  CoreConfig config("token", "test_service", "test_env");
  config.SetInitialTrackingConsent(TrackingConsent::Granted);
  config.SetApplicationVersion("1.0.0");
  HttpContext context(config);
  std::string result_url;

  SECTION("M concatenate origin and path W path has no query parameters") {
    context.BuildRequestURL("/api/v1/logs", false, result_url);
    REQUIRE(result_url == "https://browser-intake-datadoghq.com/api/v1/logs");
  }

  SECTION("M append ddsource parameter W with_ddsource is true and path has no query") {
    context.BuildRequestURL("/api/v1/logs", true, result_url);
    REQUIRE(
        result_url == "https://browser-intake-datadoghq.com/api/v1/logs?ddsource=rum-cpp"
    );
  }

  SECTION(
      "M append ddsource with ampersand W with_ddsource is true and path has query"
  ) {
    context.BuildRequestURL("/api/v1/logs?existing=param", true, result_url);
    REQUIRE(
        result_url ==
        "https://browser-intake-datadoghq.com/api/v1/"
        "logs?existing=param&ddsource=rum-cpp"
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

TEST_CASE("HttpContext BuildRequestHeaders", "[unit]") {
  // Given an ordinary config
  CoreConfig config("test_client_token_456", "test_service", "production");
  config.SetInitialTrackingConsent(TrackingConsent::Granted);
  config.SetApplicationVersion("2.1.0");
  HttpContext context(config);
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
    REQUIRE(result_headers.find("DD-EVP-ORIGIN: rum-cpp\n") != std::string::npos);

    // And DD-REQUEST-ID is set to a random valid, nonzero UUID
    const size_t request_id_pos = result_headers.find("DD-REQUEST-ID: ");
    REQUIRE(request_id_pos != std::string::npos);
    const size_t request_id_value_pos = request_id_pos + 15;  // len('DD-REQUEST-ID')
    auto request_id = UUID::Parse(result_headers.substr(request_id_value_pos, 36));
    REQUIRE(request_id.has_value());
    REQUIRE(*request_id != UUID::Zero);

    // TODO: Update when real User-Agent generation is implemented
    REQUIRE(result_headers.find("User-Agent: nobody\n") != std::string::npos);

    // And DD-EVP-ORIGIN-VERSION matches sdk_version on the context (defaults to
    // SDK_VERSION stamped into library build)
    std::string expected_version_header = "DD-EVP-ORIGIN-VERSION: ";
    expected_version_header += context.sdk_version;
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

TEST_CASE("HttpContext additional_configuration overrides", "[unit]") {
  SECTION("M use rum-cpp as default source W no additional_configuration set") {
    CoreConfig config("token", "service", "env");
    HttpContext context(config);
    REQUIRE(context.source == "rum-cpp");
  }

  SECTION("M override source W _dd.source set in additional_configuration") {
    CoreConfig config("token", "service", "env");
    config.AddAdditionalConfiguration("_dd.source", "unity");
    HttpContext context(config);
    REQUIRE(context.source == "unity");
  }

  SECTION("M override sdk_version W _dd.sdk_version set in additional_configuration") {
    CoreConfig config("token", "service", "env");
    config.AddAdditionalConfiguration("_dd.sdk_version", "99.0.0");
    HttpContext context(config);
    REQUIRE(context.sdk_version == "99.0.0");
  }

  SECTION("M use SDK_VERSION as default sdk_version W no additional_configuration set") {
    CoreConfig config("token", "service", "env");
    HttpContext context(config);
    REQUIRE(context.sdk_version == std::string(SDK_VERSION));
  }

  SECTION("M reflect overridden source in request URL W _dd.source set") {
    CoreConfig config("token", "service", "env");
    config.AddAdditionalConfiguration("_dd.source", "unity");
    HttpContext context(config);
    std::string url;
    context.BuildRequestURL("/api/v1/rum", true, url);
    REQUIRE(url.find("ddsource=unity") != std::string::npos);
  }

  SECTION("M reflect overridden source in request headers W _dd.source set") {
    CoreConfig config("token", "service", "env");
    config.AddAdditionalConfiguration("_dd.source", "unity");
    HttpContext context(config);
    std::string headers;
    context.BuildRequestHeaders("application/json", "", headers);
    REQUIRE(headers.find("DD-EVP-ORIGIN: unity\n") != std::string::npos);
  }

  SECTION("M reflect overridden sdk_version in request headers W _dd.sdk_version set") {
    CoreConfig config("token", "service", "env");
    config.AddAdditionalConfiguration("_dd.sdk_version", "99.0.0");
    HttpContext context(config);
    std::string headers;
    context.BuildRequestHeaders("application/json", "", headers);
    REQUIRE(headers.find("DD-EVP-ORIGIN-VERSION: 99.0.0\n") != std::string::npos);
  }
}

TEST_CASE("CoreContext with OS information", "[unit]") {
  SECTION("M initialize with valid OsInfo W OsInfo provided") {
    // Given valid OS info and device info
    platform::OsInfo os_info{"TestOS", "1.2.3", "12345", "1"};
    platform::DeviceInfo device_info{
        "desktop",
        "test-device",
        "test-model",
        "test-brand",
        "x86_64",
        "en-US",
        "America/New_York"
    };

    // When CoreContext is constructed with OS info and device info
    CoreConfig config("token", "service", "env");
    CoreContext ctx(config, os_info, device_info);

    // Then OS info is accessible and matches
    REQUIRE(ctx.os != nullptr);
    REQUIRE(ctx.os->name == "TestOS");
    REQUIRE(ctx.os->version == "1.2.3");
    REQUIRE(ctx.os->build == "12345");
    REQUIRE(ctx.os->version_major == "1");

    // And device info is accessible and matches
    REQUIRE(ctx.device != nullptr);
    REQUIRE(ctx.device->type == "desktop");
    REQUIRE(ctx.device->name == "test-device");
    REQUIRE(ctx.device->model == "test-model");
    REQUIRE(ctx.device->brand == "test-brand");
    REQUIRE(ctx.device->architecture == "x86_64");
    REQUIRE(ctx.device->locale == "en-US");
    REQUIRE(ctx.device->time_zone == "America/New_York");
  }
}

TEST_CASE("CoreContextProvider with OS information", "[unit]") {
  SECTION("M provide OS info via Get W CoreContext has OS info") {
    // Given a CoreContext with OS info and device info
    platform::OsInfo os_info{"ProviderTestOS", "2.0.0", "", "2"};
    platform::DeviceInfo device_info{
        "desktop",
        "test-device",
        "test-model",
        "test-brand",
        "x86_64",
        "en-US",
        "America/New_York"
    };
    CoreConfig config("token", "service", "env");
    CoreContext ctx(config, os_info, device_info);

    // When CoreContextProvider is created and Get() is called
    CoreContextProvider provider(ctx);
    CoreContext retrieved_ctx = provider.Get();

    // Then OS info is accessible via the retrieved context
    REQUIRE(retrieved_ctx.os != nullptr);
    REQUIRE(retrieved_ctx.os->name == "ProviderTestOS");
    REQUIRE(retrieved_ctx.os->version == "2.0.0");

    // And device info is accessible via the retrieved context
    REQUIRE(retrieved_ctx.device != nullptr);
    REQUIRE(retrieved_ctx.device->type == "desktop");
    REQUIRE(retrieved_ctx.device->name == "test-device");
    REQUIRE(retrieved_ctx.device->model == "test-model");
    REQUIRE(retrieved_ctx.device->brand == "test-brand");
    REQUIRE(retrieved_ctx.device->architecture == "x86_64");
    REQUIRE(retrieved_ctx.device->locale == "en-US");
    REQUIRE(retrieved_ctx.device->time_zone == "America/New_York");
  }
}
