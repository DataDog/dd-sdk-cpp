// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>

namespace datadog::platform {

/**
 * Generates a random UUID, writing 16 bytes into the provided buffer.
 *
 * The generated UUID should be suitable for use as a unique identifier (e.g.
 * session IDs, request IDs). It does not need to conform to any specific UUID
 * version, but the output should be sufficiently random to avoid collisions.
 *
 * This function must be thread-safe.
 */
void UuidGenerate(uint8_t out[16]);

}  // namespace datadog::platform
