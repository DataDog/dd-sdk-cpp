#ifndef DATADOG_INCLUDE_BENCHMARKS_SUPPORT_ENTRY
#define DATADOG_INCLUDE_BENCHMARKS_SUPPORT_ENTRY

#include <inttypes.h>
#include <stdbool.h>

#include "support/server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct benchmark_opts {
  const char* command;
  int command_argc;
  char** command_argv;
  bool help;

  const char* intake;
  server_opts_t server;
  const char* client_token;
  const char* version;

  bool repeat;
} benchmark_opts_t;

void benchmark_print_usage(const char* argv_0);
benchmark_opts_t benchmark_opts_parse(int argc, char* argv[]);
void benchmark_opts_announce(const benchmark_opts_t* opts);

bool benchmark_interrupted(void);

typedef int (*benchmark_func_t)(const void*, const void*);
int benchmark_main(
    const benchmark_opts_t* opts, benchmark_func_t f, const void* b, const void* config
);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_BENCHMARKS_SUPPORT_ENTRY
