#ifndef DATADOG_INCLUDE_RUM_H
#define DATADOG_INCLUDE_RUM_H

#include <stddef.h>

#include "datadog/api.h"
#include "datadog/attribute.h"
#include "datadog/core.h"

#ifdef __cplusplus
extern "C" {
#endif

// === RUM configuration ===

/**
 * RUM configuration struct: passed to dd_rum_init() to configure the details of
 * the RUM feature on initialization. Use dd_rum_config_create() to create a
 * default-initialized config; you MUST call dd_rum_config_destroy() when finished.
 */
typedef struct dd_rum_config dd_rum_config_t;

/**
 * Initializes a new dd_rum_config_t with default settings. MUST be matched with a
 * call to dd_rum_config_destroy().
 */
DATADOG_API dd_rum_config_t* dd_rum_config_create(void);

/**
 * Frees all memory allocated for the given RUM config.
 */
DATADOG_API void dd_rum_config_destroy(dd_rum_config_t* config);

// === RUM feature interface ===

/**
 * Interface to the Datadog SDK's RUM feature. Use dd_rum_init() to register the
 * RUM feature with the core. You MUST call dd_rum_destroy() when done.
 */
typedef struct dd_rum dd_rum_t;

/**
 * Registers the RUM feature with the core of the Datadog SDK. MUST be matched with
 * a call to dd_rum_destroy().
 *
 * If config is NULL, the default configuration values will be used.
 */
DATADOG_API dd_rum_t* dd_rum_init(dd_core_t* core, const dd_rum_config_t* config);

/**
 * Frees all memory allocated for the RUM feature reference, rendering it no
 * longer usable. May be called at any time.
 */
DATADOG_API void dd_rum_destroy(dd_rum_t* rum);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_RUM_H
