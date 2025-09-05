#ifndef DATADOG_INCLUDE_BENCHMARKS_SUPPORT_LOGGING
#define DATADOG_INCLUDE_BENCHMARKS_SUPPORT_LOGGING

#include <inttypes.h>

#include "support/entry.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parameters supplied to configure the 'logging' benchmark.
 */
typedef struct benchmark_logging {
  int32_t duration_ms;
  int32_t events_per_second;
  double burstiness_cv;
} benchmark_logging_t;

void benchmark_logging_print_usage(const char* argv_0);
benchmark_logging_t benchmark_logging_parse(int argc, char* argv[]);
void benchmark_logging_announce(
    const benchmark_opts_t* opts, const benchmark_logging_t* b
);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_BENCHMARKS_SUPPORT_LOGGING
