// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>

namespace datadog::impl {

static constexpr size_t kMaxCachedModules = 256;
static constexpr size_t kMaxBuildIdLength = 64;

/**
 * Cache entry for single module's build ID.
 *
 * Fixed-size structure designed for async-signal-safe access from crash handlers. Each
 * entry stores the module's base address (used as lookup key) and its corresponding
 * build ID string.
 */
struct CachedModuleBuildId {
  uintptr_t base_address;            // Module load address (lookup key)
  char build_id[kMaxBuildIdLength];  // Null-terminated build ID string
  bool valid;                        // Entry contains valid data
};

/**
 * Global cache of build IDs for loaded modules.
 *
 * The cache is populated at initialization time by enumerating loaded modules and
 * extracting their build IDs from PE (Windows) or ELF (Linux) files. At crash time, the
 * crash handler performs a simple linear search to find cached build IDs.
 */
struct ModuleBuildIdCache {
  CachedModuleBuildId entries[kMaxCachedModules];
  size_t num_entries;
};

// Global cache instance (zero-initialized)
extern ModuleBuildIdCache g_module_build_id_cache;

/**
 * Initializes the build ID cache as part of crash handler initialization, allowing
 * later lookup of cached build IDs in the signal-safe crash handler path. Not
 * signal-safe.
 *
 * Enumerates currently loaded modules, opens each binary file for read, and extracts
 * build IDs, and stores an entry for each binary in a fixed-size global array.
 *
 * On Windows, reads build ID from PE headers. On Linux, reads from ELF headers.
 *
 * On macOS, this is a no-op, as build IDs can be trivially resolved during the crash
 * using dyld APIs.
 */
void PopulateBuildIdCache();

/**
 * Retrieves a build ID value that was previously cached for the module loaded at the
 * given address. Signal-safe.
 *
 * If no entry exists in the cache with the given address, returns nullptr.
 */
const char* FindCachedBuildId(uintptr_t base_address);

}  // namespace datadog::impl
