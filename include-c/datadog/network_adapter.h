// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2026-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_NETWORK_ADAPTER_H
#define DATADOG_INCLUDE_NETWORK_ADAPTER_H

#include "datadog/api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle that owns a reference to a network adapter implementation.
 */
typedef struct dd_network_adapter dd_network_adapter_t;

/**
 * Releases the network adapter handle. If `adapter` is NULL, has no effect.
 */
DATADOG_API void dd_network_adapter_destroy(dd_network_adapter_t* adapter);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_NETWORK_ADAPTER_H
