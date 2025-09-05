#include "support/benchmark_startup.h"

#include <iostream>

#include "support/args.hpp"
#include "support/entry.h"

static const Benchmark STARTUP{
    "startup", "Measures the time taken to initialize the SDK", {}
};

extern "C" {

void benchmark_startup_print_usage(const char* argv_0) {
  PrintBenchmarkUsage(STARTUP, argv_0);
}

benchmark_startup_t benchmark_startup_parse(int argc, char* argv[]) {
  auto values = ParseBenchmarkArgs(STARTUP.params, argc, argv);
  benchmark_startup_t b;
  (void)values;
  return b;
}

void benchmark_startup_announce(
    const benchmark_opts_t* opts, const benchmark_startup_t* b
) {
  std::cout << "Benchmark: " << STARTUP.name << "\n";
  std::cout << "  " << STARTUP.description << "\n";
  benchmark_opts_announce(opts);

  std::cout << "Options:\n";
  (void)b;
  std::cout << "  <none>\n";
}
}
