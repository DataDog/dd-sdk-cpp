#include "support/benchmark_logging.h"

#include <iostream>

#include "support/args.hpp"
#include "support/entry.h"

static const Benchmark LOGGING{
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

extern "C" {

void benchmark_logging_print_usage(const char* argv_0) {
  PrintBenchmarkUsage(LOGGING, argv_0);
}

benchmark_logging_t benchmark_logging_parse(int argc, char* argv[]) {
  auto values = ParseBenchmarkArgs(LOGGING.params, argc, argv);
  benchmark_logging_t b;
  b.duration_ms = values[0].i;
  b.events_per_second = values[1].i;
  b.burstiness_cv = values[2].d;
  return b;
}

void benchmark_logging_announce(
    const benchmark_opts_t* opts, const benchmark_logging_t* b
) {
  std::cout << "Benchmark: " << LOGGING.name << "\n";
  std::cout << "  " << LOGGING.description << "\n";
  benchmark_opts_announce(opts);

  std::cout << "Options:\n";
  std::cout << "  duration-ms: " << b->duration_ms << "\n";
  std::cout << "  events-per-second: " << b->events_per_second << "\n";
  std::cout << "  burstiness-cv: " << b->burstiness_cv << "\n";
}
}
