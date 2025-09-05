#include "config.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

dd_core_config_t init_config(const benchmark_opts_t* opts) {
  dd_site_t site = DD_SITE_US1;
  const char* custom_endpoint = NULL;
  if (strstr(opts->intake, "dd:") == opts->intake) {
    const char* site_name = opts->intake + 3;
    if (strcmp(site_name, "us1") == 0) {
      site = DD_SITE_US1;
    } else if (strcmp(site_name, "us3") == 0) {
      site = DD_SITE_US3;
    } else if (strcmp(site_name, "us5") == 0) {
      site = DD_SITE_US5;
    } else if (strcmp(site_name, "eu1") == 0) {
      site = DD_SITE_EU1;
    } else if (strcmp(site_name, "ap1") == 0) {
      site = DD_SITE_AP1;
    } else if (strcmp(site_name, "ap2") == 0) {
      site = DD_SITE_AP2;
    } else if (strcmp(site_name, "us1-fed") == 0) {
      site = DD_SITE_US1_FED;
    } else {
      fprintf(stderr, "invalid site name '%s'\n", site_name);
      exit(1);  // NOLINT(concurrency-mt-unsafe)
    }
  } else if (strstr(opts->intake, "http://") == opts->intake ||
             strstr(opts->intake, "https://") == opts->intake) {
    custom_endpoint = opts->intake;
  } else if (strcmp(opts->intake, "mock") == 0) {
    static char mock_server_endpoint[64] = {0};
    snprintf(
        mock_server_endpoint, sizeof(mock_server_endpoint), "http://127.0.0.1:%d",
        opts->server.port
    );
    custom_endpoint = mock_server_endpoint;
  } else {
    fprintf(stderr, "invalid intake '%s'\n", opts->intake);
    exit(1);  // NOLINT(concurrency-mt-unsafe)
  }

  dd_core_config_t config = {
      .tracking_consent = DD_TRACKING_CONSENT_GRANTED,
      .datadog_site = site,
      .client_token = opts->client_token,
      .service = "dd-sdk-cpp",
      .env = "benchmark-c",
      .application_version = opts->version,
      .batch_size = DD_BATCH_SIZE_SMALL,
      .upload_frequency = DD_UPLOAD_FREQUENCY_FREQUENT,
      .batch_processing_level = DD_BATCH_PROCESSING_LEVEL_HIGH,
      .num_http_requests_per_feature_to_flush_on_stop = 1,
      .custom_endpoint_url = custom_endpoint
  };
  return config;
}
