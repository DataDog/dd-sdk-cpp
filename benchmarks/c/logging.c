#include "logging.h"

#include <stdio.h>

#include "config.h"
#include "datadog/core.h"
#include "datadog/logging.h"
#include "support/benchmark_logging.h"

static int run_logging_benchmark(const void* b_, const void* config_) {
  const benchmark_logging_t* b = (const benchmark_logging_t*)b_;
  const dd_core_config_t* config = (const dd_core_config_t*)config_;

  // Validate args
  if (b->events_per_second <= 0) {
    fprintf(stderr, "events-per-second must be >0\n");
    return 1;
  }

  // Initialize the logging API and start the core
  dd_core_t* core = dd_core_create(config);
  if (!core) {
    return 1;
  }
  dd_logging_t* logging = dd_logging_init(core);
  if (!logging) {
    dd_core_destroy(core);
    return 1;
  }
  if (!dd_core_start(core)) {
    dd_logging_destroy(logging);
    dd_core_destroy(core);
    return 2;
  }

  // Create a single logger
  dd_logger_t* logger = dd_logger_create(logging, NULL);
  if (!logger) {
    dd_logging_destroy(logging);
    dd_core_destroy(core);
    return 1;
  }

  // It's impractical to implement benchmarks in pure C: there's no compelling benefit
  // vs. just using the C API in C++, where we can access std::chrono, std::thread,
  // std::gamma_distribution, etc. without having to provide C-compatible wrappers
  dd_logger_info(logger, "Benchmark log message 0");

  // Cleanup
  dd_logger_destroy(logger);
  dd_logging_destroy(logging);
  dd_core_destroy(core);

  // TODO: Delete this benchmark, get rid of separate C benchmark program and just
  // provide a single C++ program that can exercise both APIs
  printf("logging benchmark not fully implemented in C; use C++ benchmark\n");
  return 1;
}

int logging_main(const char* argv_0, const benchmark_opts_t* opts) {
  // If called with --help/-h, print usage and exit
  if (opts->help) {
    benchmark_logging_print_usage(argv_0);
    return 1;
  }

  // Parse command-line args for this benchmark
  int argc = opts->command_argc;
  char** argv = opts->command_argv;
  benchmark_logging_t b = benchmark_logging_parse(argc, argv);

  // Print a summary of the configured benchmark to stdout
  benchmark_logging_announce(opts, &b);

  // Prepare SDK config and run our benchmark as configured in global opts
  dd_core_config_t config = init_config(opts);
  return benchmark_main(opts, run_logging_benchmark, &b, &config);
}
