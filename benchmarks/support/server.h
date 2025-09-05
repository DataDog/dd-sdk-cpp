#ifndef DATADOG_INCLUDE_BENCHMARKS_SUPPORT_SERVER
#define DATADOG_INCLUDE_BENCHMARKS_SUPPORT_SERVER

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct server_opts {
  bool enabled;
  int32_t port;
  int32_t response_delay_ms;
  int32_t response_delay_variability_ms;
} server_opts_t;

void server_start(const server_opts_t* opts);
void server_stop(void);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_BENCHMARKS_SUPPORT_SERVER
