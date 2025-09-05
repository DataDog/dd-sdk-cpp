#include <stdio.h>

#include "datadog.h"

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  printf("Datadog Native SDK C Example\n");

  dd_core_config_t config = {
      .tracking_consent = DD_TRACKING_CONSENT_GRANTED,
      .datadog_site = DD_SITE_US1,
      .client_token = "fake-client-token",
      .service = "example-service",
      .env = "development",
      .application_version = "1.0.0",
      .batch_size = DD_BATCH_SIZE_MEDIUM,
      .upload_frequency = DD_UPLOAD_FREQUENCY_AVERAGE,
      .batch_processing_level = DD_BATCH_PROCESSING_LEVEL_MEDIUM,
      .num_http_requests_per_feature_to_flush_on_stop = 1
  };

  dd_core_t *core = dd_core_create(&config);
  if (!core) {
    printf("Failed to create dd_core\n");
    return 1;
  }

  dd_logging_t *logging = dd_logging_init(core);
  if (!logging) {
    printf("Failed to register logging\n");
    return 1;
  }

  printf("Starting Datadog core...\n");
  if (!dd_core_start(core)) {
    printf("Failed to start core\n");
    return 1;
  }

  printf("Core started successfully. Shutting down...\n");
  dd_core_stop(core);

  dd_core_destroy(core);
  printf("Example completed successfully\n");

  return 0;
}
