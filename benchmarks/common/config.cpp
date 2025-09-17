// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include "common/config.hpp"

#include <cstring>
#include <iostream>

#include "common/exit.hpp"
#include "common/global.hpp"

static bool parse_dd_site(const char* intake, dd_site_t& out_site) {
  if (std::strstr(intake, "dd:") == intake) {
    const char* site_name = intake + 3;
    if (std::strcmp(site_name, "us1") == 0) {
      out_site = DD_SITE_US1;
      return true;
    }
    if (std::strcmp(site_name, "us3") == 0) {
      out_site = DD_SITE_US3;
      return true;
    }
    if (std::strcmp(site_name, "us5") == 0) {
      out_site = DD_SITE_US5;
      return true;
    }
    if (std::strcmp(site_name, "eu1") == 0) {
      out_site = DD_SITE_EU1;
      return true;
    }
    if (std::strcmp(site_name, "ap1") == 0) {
      out_site = DD_SITE_AP1;
      return true;
    }
    if (std::strcmp(site_name, "ap2") == 0) {
      out_site = DD_SITE_AP2;
      return true;
    }
    if (std::strcmp(site_name, "us1-fed") == 0) {
      out_site = DD_SITE_US1_FED;
      return true;
    }
    std::cerr << "invalid site name '" << site_name << "'\n";
    Exit(1);
  }
  return false;
}

static void parse_intake(
    const GlobalOptions& opts, dd_site_t& out_site, const char*& out_custom_endpoint
) {
  // If intake is 'dd:us1' or similar, populate out_site and we're done
  if (parse_dd_site(opts.intake, out_site)) {
    return;
  }

  // If intake an HTTP origin, set out_custom_endpoint
  if (std::strstr(opts.intake, "http://") == opts.intake ||
      std::strstr(opts.intake, "https://") == opts.intake) {
    out_custom_endpoint = opts.intake;
    return;
  }

  // If intake is 'mock', set out_custom_endpoint to the address where we're running our
  // mock HTTP server
  if (std::strcmp(opts.intake, "mock") == 0) {
    static char mock_server_endpoint[64] = {0};
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    snprintf( // NOLINT(cppcoreguidelines-pro-type-vararg)
        mock_server_endpoint, sizeof(mock_server_endpoint), "http://127.0.0.1:%d",
        opts.server.port
    );
    out_custom_endpoint = mock_server_endpoint;
    // NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    return;
  }

  // Any other value for 'intake' is invalid
  std::cerr << "invalid intake '" << opts.intake << "'\n";
  Exit(1);
}

dd_core_config_t InitConfigForC(const GlobalOptions& opts) {
  dd_site_t site = DD_SITE_US1;
  const char* custom_endpoint = nullptr;
  parse_intake(opts, site, custom_endpoint);

  dd_core_config_t config;
  dd_core_config_init(&config, opts.client_token, "dd-sdk-cpp", "benchmark-c");
  dd_core_config_set_initial_tracking_consent(&config, DD_TRACKING_CONSENT_GRANTED);
  dd_core_config_set_site(&config, site);
  dd_core_config_set_application_version(&config, opts.version);
  dd_core_config_set_batch_size(&config, DD_BATCH_SIZE_SMALL);
  dd_core_config_set_upload_frequency(&config, DD_UPLOAD_FREQUENCY_FREQUENT);
  dd_core_config_set_batch_processing_level(&config, DD_BATCH_PROCESSING_LEVEL_HIGH);
  config.internal_options.flush_http_requests_on_stop = true;
  config.internal_options.custom_endpoint_url = custom_endpoint;
  return config;
}

datadog::CoreConfig InitConfigForCpp(const GlobalOptions& opts) {
  dd_site_t site = DD_SITE_US1;
  const char* custom_endpoint = nullptr;
  parse_intake(opts, site, custom_endpoint);

  std::string_view client_token = opts.client_token ? opts.client_token : "";
  std::string_view application_version = opts.version ? opts.version : "";
  std::string_view custom_endpoint_url = custom_endpoint ? custom_endpoint : "";

  return datadog::CoreConfig(client_token, "dd-sdk-cpp", "benchmark-cpp")
      .SetInitialTrackingConsent(datadog::TrackingConsent::Granted)
      .SetSite(static_cast<datadog::Site>(site))
      .SetApplicationVersion(application_version)
      .SetBatchSize(datadog::BatchSize::Small)
      .SetUploadFrequency(datadog::UploadFrequency::Frequent)
      .SetBatchProcessingLevel(datadog::BatchProcessingLevel::High)
      .Internal_FlushHttpRequestsOnStop()
      .Internal_UseCustomEndpoint(custom_endpoint_url);
}

const dd_core_config_t& ParseConfigForC(const void* config) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return *(reinterpret_cast<const dd_core_config_t*>(config));
}

const datadog::CoreConfig& ParseConfigForCpp(const void* config) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return *(reinterpret_cast<const datadog::CoreConfig*>(config));
}
