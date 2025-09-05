#include <stdio.h>
#include <string.h>

#include "logging.h"
#include "startup.h"
#include "support/entry.h"
#include "support/server.h"

typedef int (*entrypoint_func_t)(const char*, const benchmark_opts_t*);

int main(int argc, char* argv[]) {
  // Parse command-line args
  benchmark_opts_t opts = benchmark_opts_parse(argc, argv);
  if (!opts.command) {
    benchmark_print_usage(argv[0]);
    return 1;
  }

  // Resolve the entry point for our chosen benchmark command
  entrypoint_func_t func = NULL;
  if (strcmp(opts.command, "startup") == 0) {
    func = startup_main;
  } else if (strcmp(opts.command, "logging") == 0) {
    func = logging_main;
  }

  // If the command name was unrecognized, print general usage and abort
  if (!func) {
    benchmark_print_usage(argv[0]);
    return 1;
  }

  // If we're configured to spawn a mock HTTP server in a child process, and we're not
  // just printing help, start that server
  if (opts.server.enabled && !opts.help) {
    server_start(&opts.server);
  }

  // Defer to our benchmark-specific entry point to handle the command
  int result = func(argv[0], &opts);

  // Shut down the server, if we started one
  if (opts.server.enabled && !opts.help) {
    server_stop();
  }

  // Propagate the benchmark's exit code
  return result;
}
