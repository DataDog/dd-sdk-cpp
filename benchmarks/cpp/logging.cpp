#include "logging.hpp"

#include <chrono>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

#include "config.hpp"
#include "datadog/core.hpp"
#include "datadog/logging.hpp"
#include "support/benchmark_logging.h"
#include "support/entry.h"

namespace {

int RunLoggingBenchmark(const void* b_, const void* config_) {
  // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
  const benchmark_logging_t& b = *(reinterpret_cast<const benchmark_logging_t*>(b_));
  const datadog::CoreConfig& config =
      *(reinterpret_cast<const datadog::CoreConfig*>(config_));
  // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)

  // Validate args
  if (b.events_per_second <= 0) {
    std::cerr << "events-per-second must be >0\n";
    return 1;
  }

  // Initialize the logging API and start the core
  auto core = datadog::Core::Create(config);
  if (!core) {
    return 1;
  }
  auto logging = datadog::Logging::Register(*core);
  if (!logging) {
    return 1;
  }
  if (!core->Start()) {
    return 2;
  }

  // Create single logger
  auto logger =
      logging->CreateLogger(datadog::LoggerConfig().SetName("benchmark-logger"));

  // Run benchmark with bursty timing in main thread
  auto start_time = std::chrono::steady_clock::now();
  auto stop_time = start_time + std::chrono::milliseconds(b.duration_ms);

  // Calculate mean interval between events
  double mean_interval_ms = 1000.0 / b.events_per_second;

  // Random number generator
  std::mt19937 gen(0x123beb02);

  // Gamma distribution for inter-arrival times
  const double cv_squared = b.burstiness_cv * b.burstiness_cv;
  const bool use_gamma_distribution = cv_squared > 0.0000001;
  double shape = use_gamma_distribution ? 1.0 / cv_squared : 1.0;
  double scale = mean_interval_ms * cv_squared;
  std::gamma_distribution<double> interval_dist(shape, scale);

  int event_count = 0;
  while (std::chrono::steady_clock::now() < stop_time && !benchmark_interrupted()) {
    // Sample next interval and sleep
    double interval_ms = use_gamma_distribution ? interval_dist(gen) : mean_interval_ms;
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(interval_ms));

    // Check if we've exceeded duration after sleep or been interrupted
    if (std::chrono::steady_clock::now() >= stop_time || benchmark_interrupted()) {
      break;
    }

    // Emit log event
    std::ostringstream msg;
    msg << "Benchmark log message " << ++event_count;
    logger->Info(msg.str());
  }

  core->Stop();
  return 0;
}

}  // namespace

int LoggingMain(const char* argv_0, const benchmark_opts_t& opts) {
  // If called with --help/-h, print usage and exit
  if (opts.help) {
    benchmark_logging_print_usage(argv_0);
    return 1;
  }

  // Parse command-line args for this benchmark
  int argc = opts.command_argc;
  char** argv = opts.command_argv;
  benchmark_logging_t b = benchmark_logging_parse(argc, argv);

  // Print a summary of the configured benchmark to stdout
  benchmark_logging_announce(&opts, &b);

  // Prepare SDK config and run our benchmark as configured in global opts
  datadog::CoreConfig config = InitConfig(opts);
  return benchmark_main(&opts, RunLoggingBenchmark, &b, &config);
}
