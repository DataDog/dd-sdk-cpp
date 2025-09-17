// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "common/global.hpp"

#include <array>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstring>
#include <iostream>
#include <limits>

#include "common/arg.hpp"
#include "common/benchmark.hpp"
#include "common/exit.hpp"

static const std::array<BenchmarkParam, MAX_ARGS> GLOBAL_OPTS{
    BenchmarkParam::String(
        "intake", "mock",
        "'dd:us1' for live backend; 'http://...' for custom endpoint; 'mock' to run "
        "mock HTTP server"
    ),
    BenchmarkParam::Int(
        "mock-port", 15101, "Listen port for mock server; used only with --intake=mock"
    ),
    BenchmarkParam::Int(
        "mock-response-delay-ms", 20,
        "Base delay between mock server accepting a connection and sending a response"
    ),
    BenchmarkParam::Int(
        "mock-response-delay-variability-ms", 0,
        "Additional +/- for mock server response delay, randomized per request"
    ),
    BenchmarkParam::String(
        "client-token", "fake-client-token",
        "Client token to supply to SDK configuration; for use with live intake"
    ),
    BenchmarkParam::String(
        "version", "unknown", "Application version to supply to SDK configuration"
    ),
    BenchmarkParam::String(
        "api", "c++", "API to use in benchmark: some benchmarks only support one API"
    ),
    BenchmarkParam::Int("repeat", 0, "Repeat benchmark indefinitely until interrupted")
};

static void populate_global_opts(
    GlobalOptions& opts, const std::array<BenchmarkParamValue, MAX_ARGS>& values
) {
  opts.intake = values[0].s;
  opts.server.enabled = std::strcmp(values[0].s, "mock") == 0;
  opts.server.port = values[1].i;
  opts.server.response_delay_ms = values[2].i;
  opts.server.response_delay_variability_ms = values[3].i;
  opts.client_token = values[4].s;
  opts.version = values[5].s;

  const char* api_str = values[6].s;
  if (std::strcmp(api_str, "c") == 0) {
    opts.api = Api::C;
  } else if (std::strcmp(api_str, "cpp") == 0 || std::strcmp(api_str, "c++") == 0) {
    opts.api = Api::Cpp;
  } else {
    std::cerr << "Invalid --api option: " << api_str << "\n";
    Exit(1);
  }

  opts.repeat = values[7].i != 0;
}

GlobalOptions GlobalOptions::Parse(int argc, char* argv[]) {
  // Find the first positional arg,
  int positional_arg = -1;
  for (int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    const size_t arg_len = std::strlen(arg);
    if (arg_len <= 1 || arg[0] != '-') {
      positional_arg = i;
      break;
    }
  }
  if (positional_arg == -1) {
    PrintGlobalUsage(argv[0]);
    Exit(1);
  }

  // The first positional arg is the name of the benchmark command; the remaining args
  // modify the command
  GlobalOptions opts;  // NOLINT(cppcoreguidelines-pro-type-member-init)
  opts.command = argv[positional_arg];
  opts.command_argc = argc - positional_arg - 1;
  opts.command_argv = argv + positional_arg + 1;

  // If --help/-h appears anywhere, set the 'help' flag
  opts.help = false;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      opts.help = true;
    }
  }

  // Parse any args between the binary name and the command name as global options
  int global_argc = positional_arg - 1;
  char** global_argv = argv + 1;
  auto values = ParseBenchmarkParams(GLOBAL_OPTS, global_argc, global_argv);
  populate_global_opts(opts, values);
  return opts;
}

void GlobalOptions::Announce() const {
  std::cout << "Global options:\n";
  std::cout << "  intake: " << intake << "\n";
  if (server.enabled) {
    std::cout << "  mock-port: " << server.port << "\n";
    std::cout << "  mock-response-delay-ms: " << server.response_delay_ms << "\n";
    std::cout << "  mock-response-delay-variability-ms: "
              << server.response_delay_variability_ms << "\n";
  }
  std::cout << "  client-token: ";
  if (std::strstr(client_token, "fake-") == client_token) {
    std::cout << client_token;
  } else {
    std::cout << "<redacted>";
  }
  std::cout << "\n";
  std::cout << "  version: " << version << "\n";
  if (api == Api::C) {
    std::cout << "  api: c\n";
  } else {
    std::cout << "  api: c++\n";
  }
  std::cout << "  repeat: " << (repeat ? "true" : "false") << "\n";
}

void PrintGlobalUsage(const char* argv_0) {
  std::cout << "Datadog SDK | Benchmark Runner\n";
  std::cout << "Usage:\n";
  std::cout << "  " << argv_0 << " [global-opts] <benchmark> [benchmark-opts]\n";
  std::cout << "Global options:\n";
  for (const BenchmarkParam& p : GLOBAL_OPTS) {
    if (!p.name) {
      continue;
    }
    std::cout << "  --" << p.name << "=";
    switch (p.type) {
      case BenchmarkParamType::Int:
        std::cout << p.default_value.i;
        break;
      case BenchmarkParamType::Double:
        std::cout << p.default_value.d;
        break;
      case BenchmarkParamType::String:
        std::cout << p.default_value.s;
        break;
    }
    std::cout << ": " << p.description << "\n";
  }
  std::cout << "Available benchmarks:\n";
  std::cout << "  startup\n";
  std::cout << "  logging\n";
  std::cout << "For more information, supply -h with a benchmark, e.g.:\n";
  std::cout << "  " << argv_0 << " logging -h\n";
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool s_interrupted = false;

static void handle_sigint(int signum) {
  (void)signum;
  s_interrupted = true;
}

static void announce_start(const char* name, int i) {
  std::cout << "--- BEGIN BENCHMARK <" << name << ">[" << i << "] ---\n";
}

static void announce_end(
    const char* name, int i, int result,
    std::chrono::high_resolution_clock::duration elapsed
) {
  std::chrono::duration<double> elapsed_sec = elapsed;

  std::cout << "--- END BENCHMARK <" << name << ">[" << i << "]";
  std::cout << " (result: " << result << ")";
  std::cout << " (elapsed: " << elapsed_sec.count() << "s)";
  std::cout << " ---\n";
}

int BenchmarkMain(
    const GlobalOptions& opts, BenchmarkMainFunc f, const void* config, const void* b
) {
  // Loop indefinitely until we break
  std::signal(SIGINT, handle_sigint);
  for (int i = 0; i < std::numeric_limits<int>::max(); i++) {
    // Print a line of output announcing BEGIN BENCHMARK
    announce_start(opts.command, i);

    // Run the benchmark once, measuring elapsed time
    auto start_time = std::chrono::high_resolution_clock::now();
    const int result = f(config, b);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = end_time - start_time;

    // Print a line of output announcing END BENCHMARK
    announce_end(opts.command, i, result, elapsed);

    // If the benchmark failed, or if it's time to wrap up, return
    if (result != 0 || !opts.repeat || BenchmarkInterrupted()) {
      return result;
    }
  }

  // We exited the loop naturally; we must have run successfully INT_MAX times
  return 0;
}

bool BenchmarkInterrupted() { return s_interrupted; }
