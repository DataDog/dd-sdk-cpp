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
  // Global
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
};

void PrintGlobalUsage(const char* argv_0);

GlobalOptions ParseGlobalOptions(int argc, char* argv[]);

void AnnounceGlobalOptions(const GlobalOptions& opts);

typedef int (*BenchmarkMainFunc)(const void*, const void*);

int BenchmarkMain(
    const GlobalOptions& opts, BenchmarkMainFunc f, const void* config, const void* b
);

bool BenchmarkInterrupted();
