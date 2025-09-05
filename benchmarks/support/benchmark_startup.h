#ifndef DATADOG_INCLUDE_BENCHMARKS_SUPPORT_STARTUP
#define DATADOG_INCLUDE_BENCHMARKS_SUPPORT_STARTUP

#include <inttypes.h>

#include "support/entry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parameters supplied to configure the 'startup' benchmark.
 */
typedef struct benchmark_startup {
  void* unused;
} benchmark_startup_t;

void benchmark_startup_print_usage(const char* argv_0);
benchmark_startup_t benchmark_startup_parse(int argc, char* argv[]);
void benchmark_startup_announce(
    const benchmark_opts_t* opts, const benchmark_startup_t* b
);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_BENCHMARKS_SUPPORT_STARTUP
