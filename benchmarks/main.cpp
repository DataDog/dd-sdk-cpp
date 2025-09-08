#include <cstring>

#include "common/global.hpp"
#include "common/server.hpp"

typedef int (*BenchmarkEntrypointFunc)(const char*, const GlobalOptions&);

struct RegisteredBenchmark {
  const char* name;
  BenchmarkEntrypointFunc entrypoint;
};

extern int benchmark_startup_main(const char* argv_0, const GlobalOptions& opts);
extern int benchmark_logging_main(const char* argv_0, const GlobalOptions& opts);

static const RegisteredBenchmark REGISTERED_BENCHMARKS[] = {
    {"startup", benchmark_startup_main}, {"logging", benchmark_logging_main}
};

int main(int argc, char* argv[]) {
  // Parse command-line args
  GlobalOptions opts = ParseGlobalOptions(argc, argv);
  if (!opts.command) {
    PrintGlobalUsage(argv[0]);
    return 1;
  }

  // Resolve the entry point for our chosen benchmark command
  BenchmarkEntrypointFunc func = NULL;
  for (const RegisteredBenchmark& r : REGISTERED_BENCHMARKS) {
    if (std::strcmp(opts.command, r.name) == 0) {
      func = r.entrypoint;
      break;
    }
  }

  // If the command name was unrecognized, print general usage and abort
  if (!func) {
    PrintGlobalUsage(argv[0]);
    return 1;
  }

  // If we're configured to spawn a mock HTTP server in a child process, and we're not
  // just printing help, start that server
  if (opts.server.enabled && !opts.help) {
    StartServer(opts.server);
  }

  // Defer to our benchmark-specific entry point to handle the command
  const int result = func(argv[0], opts);

  // Shut down the server, if we started one
  if (opts.server.enabled && !opts.help) {
    StopServer();
  }

  // Propagate the benchmark's exit code
  return result;
}
