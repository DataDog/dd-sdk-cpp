#include "common/config.hpp"

#include <cstring>
#include <iostream>

#include "common/exit.hpp"
#include "common/global.hpp"

static bool parse_dd_site(const char* intake, dd_site_t& out_site) {
  if (std::strstr(intake, "dd:") != intake) {
    return false;
  }

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
  return dd_core_config_t{
      DD_TRACKING_CONSENT_GRANTED,
      site,
      opts.client_token,
      "dd-sdk-cpp",
      "benchmark-c",
      opts.version,
      DD_BATCH_SIZE_SMALL,
      DD_UPLOAD_FREQUENCY_FREQUENT,
      DD_BATCH_PROCESSING_LEVEL_HIGH,
      1,
      custom_endpoint
  };
}

datadog::CoreConfig InitConfigForCpp(const GlobalOptions& opts) {
  dd_site_t site = DD_SITE_US1;
  const char* custom_endpoint = nullptr;
  parse_intake(opts, site, custom_endpoint);
  // NOLINTBEGIN(clang-analyzer-cplusplus.StringChecker)
  return datadog::CoreConfig{
      datadog::TrackingConsent::Granted,
      static_cast<datadog::Site>(site),
      opts.client_token,
      "dd-sdk-cpp",
      "benchmark-cpp",
      opts.version,
      datadog::BatchSize::Small,
      datadog::UploadFrequency::Frequent,
      datadog::BatchProcessingLevel::High,
      1,
      custom_endpoint
  };
  // NOLINTEND(clang-analyzer-cplusplus.StringChecker)
}

const dd_core_config_t& ParseConfigForC(const void* config) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return *(reinterpret_cast<const dd_core_config_t*>(config));
}

const datadog::CoreConfig& ParseConfigForCpp(const void* config) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return *(reinterpret_cast<const datadog::CoreConfig*>(config));
}
