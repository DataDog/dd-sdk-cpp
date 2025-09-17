// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <iostream>

#include "common/benchmark.hpp"
#include "common/config.hpp"
#include "common/global.hpp"
#include "datadog.h"
#include "datadog.hpp"

static const Benchmark BENCHMARK{
    "startup", "Measures the time taken to initialize the SDK", {}
};

static int RunC(const void* config_, const void*) {
  const dd_core_config_t& config = ParseConfigForC(config_);
  dd_core_t* core = dd_core_create(&config);
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

static int RunCpp(const void* config_, const void*) {
  const datadog::CoreConfig& config = ParseConfigForCpp(config_);
  auto core = datadog::Core::Create(config);
  if (!core) {
    return 1;
  }
  auto logging = datadog::Logging::Register(core);
  if (!core->Start()) {
    return 2;
  }
  core->Stop();
  return 0;
}

int benchmark_startup_main(const char* argv_0, const GlobalOptions& opts) {
  dd_core_config_t c_config;
  datadog::CoreConfig cpp_config;

  // If called with --help/-h, print usage and return
  if (opts.help) {
    PrintBenchmarkUsage(BENCHMARK, argv_0);
    return 1;
  }

  // Print a summary of the configured benchmark to stdout
  std::cout << "Benchmark: " << BENCHMARK.name << "\n";
  std::cout << "  " << BENCHMARK.description << "\n";
  opts.Announce();

  // Determine which entrypoint to use based on configured API, and create SDK config
  void* config = nullptr;
  BenchmarkMainFunc f = nullptr;
  if (opts.api == Api::C) {
    c_config = InitConfigForC(opts);
    config = &c_config;
    f = RunC;
  } else if (opts.api == Api::Cpp) {
    cpp_config = InitConfigForCpp(opts);
    config = &cpp_config;
    f = RunCpp;
  }
  if (!config || !f) {
    std::cerr << "Benchmark " << BENCHMARK.name << " does not support configured API\n";
    return 1;
  }

  return BenchmarkMain(opts, f, config, nullptr);
}
