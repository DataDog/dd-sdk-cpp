#pragma once

#include <cinttypes>

#include "common/server.hpp"

/**
 * Selects between the SDK's available APIs.
 */
enum class Api : uint8_t { C = 0x01, Cpp = 0x02, Both = C | Cpp };

/**
 * Global options parsed from command-line args, used to run benchmark commands.
 */
struct GlobalOptions {
  // Selected command, plus args to be parsed by a benchmark-specific function
  const char* command;
  int command_argc;
  char** command_argv;

  // If true, do not run the benchmark command; instead print its usage and exit
  bool help;

  // Intake specifies 'mock', 'dd:<site>', or custom endpoint URL
  const char* intake;

  // Options for mock server: if server.enabled, the benchmark program should spawn a
  // mock HTTP server to handle requests
  ServerOptions server;

  // Additional SDK configuration details
  const char* client_token;
  const char* version;

  // Whether to select C API or C++ API version of the chosen benchmark command
  Api api;

  // If true, repeat the benchmark indefinitely until SIGTERM
  bool repeat;

  /**
   * Parses global options from a subset of command-line argument values.
   */
  static GlobalOptions Parse(int argc, char* argv[]);

  /**
   * Dumps configured global options to stdout in a human-readable format.
   */
  void Announce() const;
};

/**
 * Prints basic command usage information to stdout.
 */
void PrintGlobalUsage(const char* argv_0);

/**
 * Function that defines the entry point of a benchmark, running it for a single
 * iteration. First parameter is a const pointer to either dd_core_config_t or
 * datadog::CoreConfig; second parameter is a const pointer to a benchmark-defined
 * options type (e.g. BenchmarkOptions).
 */
typedef int (*BenchmarkMainFunc)(const void*, const void*);

/**
 * Runs the given benchmark one or more times as configured by `opts`. For config and
 * benchmark options params, caller must pass pointers to values of the types expected
 * by `f`.
 */
int BenchmarkMain(
    const GlobalOptions& opts, BenchmarkMainFunc f, const void* config, const void* b
);

/**
 * Returns true if SIGINT etc. has been received, indicating to a benchmark function
 * that it should exit ASAP.
 */
bool BenchmarkInterrupted();
