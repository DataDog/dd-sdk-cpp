#pragma once

#include <cinttypes>

/**
 * Options configuring the mock server implemented in server.py.
 */
struct ServerOptions {
  bool enabled;
  int32_t port;
  int32_t response_delay_ms;
  int32_t response_delay_variability_ms;
};

/**
 * Spawns a child process that runs server.py in the first available Python interpreter
 * found in the PATH.
 */
void StartServer(const ServerOptions& opts);

/**
 * Terminates the child server process, if one was previously started and is still
 * running.
 */
void StopServer();
