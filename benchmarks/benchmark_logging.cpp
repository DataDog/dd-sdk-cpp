// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <chrono>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

#include "common/benchmark.hpp"
#include "common/config.hpp"
#include "common/global.hpp"
#include "datadog.h"
#include "datadog.hpp"

static const Benchmark BENCHMARK{
    "logging",
    "Uses the logging API to continually generate log messages",
    {BenchmarkParam::Int("duration-ms", 5000, "Total time to spend logging"),
     BenchmarkParam::Int(
         "events-per-second", 100,
         "Total number of log events to emit per second, distributed across all "
         "loggers and threads"
     ),
     BenchmarkParam::Double(
         "burstiness-cv", 0.0,
         "Coefficient of variation for event timing: perfectly regular at 0, "
         "exponential at 1, super bursty above 1"
     )}
};

struct BenchmarkOptions {
  int32_t duration_ms;
  int32_t events_per_second;
  double burstiness_cv;

  static BenchmarkOptions Parse(int argc, char* argv[]) {
    BenchmarkOptions b;  // NOLINT(cppcoreguidelines-pro-type-member-init)
    auto values = ParseBenchmarkParams(BENCHMARK.params, argc, argv);
    b.duration_ms = values[0].i;
    b.events_per_second = values[1].i;
    b.burstiness_cv = values[2].d;
    return b;
  }

  void Announce() const {
    std::cout << "Options:\n";
    std::cout << "  duration-ms: " << duration_ms << "\n";
    std::cout << "  events-per-second: " << events_per_second << "\n";
    std::cout << "  burstiness-cv: " << burstiness_cv << "\n";
  }
};

static int RunCpp(const void* config_, const void* b_) {
  const datadog::CoreConfig& config = ParseConfigForCpp(config_);
  const BenchmarkOptions& b = ParseBenchmarkOptions<BenchmarkOptions>(b_);

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
  auto logging = datadog::Logging::Register(core);
  if (!logging) {
    return 1;
  }
  if (!core->Start()) {
    return 2;
  }

  // Create a single logger
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
  while (std::chrono::steady_clock::now() < stop_time && !BenchmarkInterrupted()) {
    // Sample next interval and sleep
    double interval_ms = use_gamma_distribution ? interval_dist(gen) : mean_interval_ms;
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(interval_ms));

    // Check if we've exceeded duration after sleep or been interrupted
    if (std::chrono::steady_clock::now() >= stop_time || BenchmarkInterrupted()) {
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

int benchmark_logging_main(const char* argv_0, const GlobalOptions& opts) {
  // If called with --help/-h, print usage and return
  if (opts.help) {
    PrintBenchmarkUsage(BENCHMARK, argv_0);
    return 1;
  }

  // Parse benchmark command options and print a summary to stdout
  const auto b = BenchmarkOptions::Parse(opts.command_argc, opts.command_argv);
  std::cout << "Benchmark: " << BENCHMARK.name << "\n";
  std::cout << "  " << BENCHMARK.description << "\n";
  opts.Announce();
  b.Announce();

  // Require C++ API for this benchmark
  if (opts.api != Api::Cpp) {
    std::cerr << "Benchmark " << BENCHMARK.name << " does not support configured API\n";
    return 1;
  }

  // Initialize config and run benchmark
  auto cpp_config = InitConfigForCpp(opts);
  return BenchmarkMain(opts, RunCpp, &cpp_config, &b);
}
