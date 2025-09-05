#include "startup.hpp"

#include <iostream>

#include "config.hpp"
#include "datadog/core.hpp"
#include "datadog/logging.hpp"
#include "support/benchmark_startup.h"

namespace {

int RunStartupBenchmark(const void* b_, const void* config_) {
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  const benchmark_startup_t& b = *(reinterpret_cast<const benchmark_startup_t*>(b_));
  const datadog::CoreConfig& config =
      *(reinterpret_cast<const datadog::CoreConfig*>(config_));
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  (void)b;

  auto core = datadog::Core::Create(config);
  if (!core) {
    return 1;
  }

  auto logging = datadog::Logging::Register(*core);

  if (!core->Start()) {
    return 2;
  }
  core->Stop();
  return 0;
}

}  // namespace

int StartupMain(const char* argv_0, const benchmark_opts_t& opts) {
  // If called with --help/-h, print usage and exit
  if (opts.help) {
    benchmark_startup_print_usage(argv_0);
    return 1;
  }

  // Parse command-line args for this benchmark
  int argc = opts.command_argc;
  char** argv = opts.command_argv;
  benchmark_startup_t b = benchmark_startup_parse(argc, argv);

  // Print a summary of the configured benchmark to stdout
  benchmark_startup_announce(&opts, &b);

  // Prepare SDK config and run our benchmark as configured in global opts
  datadog::CoreConfig config = InitConfig(opts);
  return benchmark_main(&opts, RunStartupBenchmark, &b, &config);
}
