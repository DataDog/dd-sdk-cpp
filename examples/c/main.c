// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include <stdio.h>

#include "datadog.h"

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  printf("Datadog Native SDK C Example\n");

  // Prepare our configuration and create the Datadog SDK Core
  dd_core_config_t config;
  dd_core_config_init(&config, "fake-client-token", "example-service", "development");
  dd_core_config_set_application_version(&config, "1.0.0");

  dd_core_t *core = dd_core_create(&config);
  if (!core) {
    printf("Failed to create dd_core\n");
    return 1;
  }

  // Register the logging feature
  dd_logging_t *logging = dd_logging_init(core);
  if (!logging) {
    printf("Failed to register logging\n");
    return 1;
  }

  // Create a logger (this can be done before or after Core start)
  dd_logger_t *logger = dd_logger_create(logging, NULL);
  if (!logger) {
    printf("Failed to create logger\n");
    return 1;
  }

  // Register the RUM feature
  dd_rum_t *rum = dd_rum_init(core, NULL);
  if (!rum) {
    printf("Failed to register RUM\n");
    return 1;
  }

  // Start the core to begin processing events
  printf("Starting Datadog core...\n");
  if (!dd_core_start(core)) {
    printf("Failed to start core\n");
    return 1;
  }

  // Whenever the user's tracking consent changes, convey it to the SDK
  dd_core_set_tracking_consent(core, DD_TRACKING_CONSENT_GRANTED);

  // Use our logger to send a message
  dd_logger_info(logger, "Hello world!");

  // Stop the core on application shutdown
  printf("Core started successfully. Shutting down...\n");
  dd_core_stop(core);

  // Clean up SDK resources
  dd_rum_destroy(rum);
  dd_logger_destroy(logger);
  dd_logging_destroy(logging);
  dd_core_destroy(core);
  printf("Example completed successfully\n");

  return 0;
}
