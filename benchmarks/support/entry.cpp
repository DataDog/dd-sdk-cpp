#include "support/entry.h"

#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstring>
#include <iostream>
#include <limits>

#include "support/args.hpp"

static const std::array<BenchmarkParam, MAX_ARGS> GLOBAL_OPTS{
    BenchmarkParam::String("intake", "mock", ""),
    BenchmarkParam::Int("mock-port", 15101, ""),
    BenchmarkParam::Int("mock-response-delay-ms", 20, ""),
    BenchmarkParam::Int("mock-response-delay-variability-ms", 0, ""),
    BenchmarkParam::String("client-token", "fake-client-token", ""),
    BenchmarkParam::String("version", "unknown", ""),
    BenchmarkParam::Int("repeat", 0, "")
};

static void populate_global_opts(
    benchmark_opts_t& opts, const std::array<BenchmarkParamValue, MAX_ARGS>& values
) {
  opts.intake = values[0].s;
  opts.server.enabled = std::strcmp(values[0].s, "mock") == 0;
  opts.server.port = values[1].i;
  opts.server.response_delay_ms = values[2].i;
  opts.server.response_delay_variability_ms = values[3].i;
  opts.client_token = values[4].s;
  opts.version = values[5].s;
  opts.repeat = values[6].i != 0;
}

static void announce_global_opts(const benchmark_opts_t& opts) {
  std::cout << "  intake: " << opts.intake << "\n";
  if (opts.server.enabled) {
    std::cout << "  mock-port: " << opts.server.port << "\n";
    std::cout << "  mock-response-delay-ms: " << opts.server.response_delay_ms << "\n";
    std::cout << "  mock-response-delay-variability-ms: "
              << opts.server.response_delay_variability_ms << "\n";
  }
  std::cout << "  client-token: ";
  if (std::strstr(opts.client_token, "fake-") == opts.client_token) {
    std::cout << opts.client_token;
  } else {
    std::cout << "<redacted>";
  }
  std::cout << "\n";

  std::cout << "  version: " << opts.version << "\n";
  std::cout << "  repeat: " << (opts.repeat ? "true" : "false") << "\n";
}

extern "C" {
void benchmark_print_usage(const char* argv_0) {
  std::cout << "Datadog SDK | Benchmark Runner\n";
  std::cout << "Usage:\n";
  std::cout << "  " << argv_0 << " [global-opts] <benchmark> [benchmark-opts]\n";
  std::cout << "Global options:\n";
  std::cout << "  --intake: 'dd:us1' for live backend; 'http://...' for custom "
               "endpoint; 'mock' to run mock HTTP server\n";
  std::cout
      << "  --client-token: Datadog client token, required if using live backend\n";
  std::cout << "  --version: Application version passed to SDK config\n";
  std::cout << "  --repeat: Repeat benchmark indefinitely until interrupted\n";
  std::cout << "Available benchmarks:\n";
  std::cout << "  startup\n";
  std::cout << "  logging\n";
  std::cout << "For more information, supply -h with a benchmark, e.g.:\n";
  std::cout << "  " << argv_0 << " logging -h\n";
}

benchmark_opts_t benchmark_opts_parse(int argc, char* argv[]) {
  // Find the first positional arg
  int positional_arg = -1;
  for (int i = 1; i < argc; i++) {
    auto arg = ParseArg(argv[i]);
    if (arg.name == nullptr) {
      positional_arg = i;
      break;
    }
  }
  if (positional_arg == -1) {
    benchmark_print_usage(argv[0]);
    std::exit(1);  // NOLINT(concurrency-mt-unsafe)
  }

  // The first positional arg is the command name; the remaining args modify the
  // command
  benchmark_opts_t opts;
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
  auto values = ParseBenchmarkArgs(GLOBAL_OPTS, global_argc, global_argv);
  populate_global_opts(opts, values);
  return opts;
}

void benchmark_opts_announce(const benchmark_opts_t* opts) {
  std::cout << "Global options:\n";
  announce_global_opts(*opts);
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

static bool s_interrupted = false;

static void handle_sigint(int signum) {
  (void)signum;
  s_interrupted = true;
}

bool benchmark_interrupted() { return s_interrupted; }

int benchmark_main(
    const benchmark_opts_t* opts, benchmark_func_t f, const void* b, const void* config
) {
  // Loop indefinitely until we break
  std::signal(SIGINT, handle_sigint);
  for (int i = 0; i < std::numeric_limits<int>::max(); i++) {
    // Print a line of output announcing BEGIN BENCHMARK
    announce_start(opts->command, i);

    // Run the benchmark once, measuring elapsed time
    auto start_time = std::chrono::high_resolution_clock::now();
    const int result = f(b, config);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto elapsed = end_time - start_time;

    // Print a line of output announcing END BENCHMARK
    announce_end(opts->command, i, result, elapsed);

    // If the benchmark failed, or if it's time to wrap up, return
    if (result != 0 || !opts->repeat || s_interrupted) {
      return result;
    }
  }

  // We exited the loop naturally; we must have run successfully INT_MAX times
  return 0;
}
}
