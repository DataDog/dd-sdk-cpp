#include "config.hpp"

#include <cstring>
#include <iostream>

datadog::CoreConfig InitConfig(const benchmark_opts_t& opts) {
  datadog::Site site = datadog::Site::us1;
  const char* custom_endpoint = NULL;
  if (strstr(opts.intake, "dd:") == opts.intake) {
    const char* site_name = opts.intake + 3;
    if (strcmp(site_name, "us1") == 0) {
      site = datadog::Site::us1;
    } else if (strcmp(site_name, "us3") == 0) {
      site = datadog::Site::us3;
    } else if (strcmp(site_name, "us5") == 0) {
      site = datadog::Site::us5;
    } else if (strcmp(site_name, "eu1") == 0) {
      site = datadog::Site::eu1;
    } else if (strcmp(site_name, "ap1") == 0) {
      site = datadog::Site::ap1;
    } else if (strcmp(site_name, "ap2") == 0) {
      site = datadog::Site::ap2;
    } else if (strcmp(site_name, "us1-fed") == 0) {
      site = datadog::Site::us1_fed;
    } else {
      std::cerr << "invalid site name '" << site_name << "'\n";
      exit(1);  // NOLINT(concurrency-mt-unsafe)
    }
  } else if (strstr(opts.intake, "http://") == opts.intake ||
             strstr(opts.intake, "https://") == opts.intake) {
    custom_endpoint = opts.intake;
  } else if (strcmp(opts.intake, "mock") == 0) {
    static char mock_server_endpoint[64] = {0};
    // NOLINTBEGIN
    snprintf(
        mock_server_endpoint, sizeof(mock_server_endpoint), "http://127.0.0.1:%d",
        opts.server.port
    );
    custom_endpoint = mock_server_endpoint;
    // NOLINTEND
  } else {
    std::cerr << "invalid intake '" << opts.intake << "'\n";
    exit(1);  // NOLINT(concurrency-mt-unsafe)
  }

  return datadog::CoreConfig{
      datadog::TrackingConsent::Granted,
      site,
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
}
