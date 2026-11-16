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
 *
 * The default implementations generate version 4 UUIDs: on Windows from
 * `CoCreateGuid()`, on macOS from `uuid_generate_random()`, and on Linux from
 * `getrandom()`, falling back to /dev/urandom on kernels or sandboxes where
 * `getrandom()` is unavailable. The Linux implementation never blocks: if the
 * kernel's random pool isn't initialized yet (early in boot) or no kernel source can
 * be read, it uses a non-cryptographic fallback built from the time, process and
 * thread IDs, and a counter, which never returns the same UUID twice within a
 * process.
 */
void UuidGenerate(uint8_t out[16]);

}  // namespace datadog::platform
