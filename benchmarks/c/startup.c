#include "startup.h"

#include <stdio.h>

#include "config.h"
#include "datadog/core.h"
#include "datadog/logging.h"
#include "support/benchmark_startup.h"

static int run_startup_benchmark(const void* b_, const void* config_) {
  const benchmark_startup_t* b = (const benchmark_startup_t*)b_;
  const dd_core_config_t* config = (const dd_core_config_t*)config_;

  (void)b;

  dd_core_t* core = dd_core_create(config);
  if (!core) {
    return 1;
  }

  dd_logging_t* logging = dd_logging_init(core);
  if (!logging) {
    return 1;
  }

  if (!dd_core_start(core)) {
    dd_logging_destroy(logging);
    return 2;
  }

  dd_logging_destroy(logging);
  dd_core_stop(core);
  dd_core_destroy(core);
  return 0;
}

int startup_main(const char* argv_0, const benchmark_opts_t* opts) {
  // If called with --help/-h, print usage and exit
  if (opts->help) {
    benchmark_startup_print_usage(argv_0);
    return 1;
  }

  // Parse command-line args for this benchmark
  int argc = opts->command_argc;
  char** argv = opts->command_argv;
  benchmark_startup_t b = benchmark_startup_parse(argc, argv);

  // Print a summary of the configured benchmark to stdout
  benchmark_startup_announce(opts, &b);

  // Prepare SDK config and run our benchmark as configured in global opts
  dd_core_config_t config = init_config(opts);
  return benchmark_main(opts, run_startup_benchmark, &b, &config);
}
