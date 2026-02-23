// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>

namespace datadog::platform {

static constexpr size_t kMaxCachedModules = 256;
static constexpr size_t kMaxBuildIdLength = 64;

/**
 * Cache entry for single module's build ID.
 *
 * Fixed-size structure designed for async-signal-safe access from crash handlers.
 * Each entry stores the module's base address (used as lookup key) and its
 * corresponding build ID string.
 */
struct CachedModuleBuildId {
  uintptr_t base_address;            // Module load address (lookup key)
  char build_id[kMaxBuildIdLength];  // Null-terminated build ID string
  bool valid;                        // Entry contains valid data
};

/**
 * Global cache of build IDs for loaded modules.
 *
 * Designed for async-signal-safe reads from crash handlers:
 * - Fixed-size array (no dynamic allocation at crash time)
 * - Writes only during initialization (safe context)
 * - Reads from signal handler are lock-free
 * - Stale reads during concurrent init acceptable (crashes during init are rare)
 *
 * The cache is populated at initialization time by enumerating loaded modules and
 * extracting their build IDs from PE (Windows) or ELF (Linux) files. At crash time,
 * the crash handler performs a simple linear search to find cached build IDs.
 */
struct ModuleBuildIdCache {
  CachedModuleBuildId entries[kMaxCachedModules];
  size_t num_entries;
};

// Global cache instance (zero-initialized)
extern ModuleBuildIdCache g_module_build_id_cache;

/**
 * Initialize build ID cache after crash handler setup.
 *
 * Enumerates currently loaded modules and extracts their build IDs from binary
 * files (PE on Windows, ELF on Linux). Build IDs are cached in fixed-size global
 * array for async-signal-safe lookup at crash time.
 *
 * Safe to call multiple times (reinitializes cache). Uses malloc and file I/O, so
 * this function is NOT async-signal-safe and must be called from normal context
 * before any potential crash.
 *
 * On macOS, this is a no-op (build IDs are extracted from memory at crash time).
 */
void InitializeModuleBuildIdCache();

/**
 * Lookup cached build ID for module at base address.
 *
 * Performs linear search through cache to find entry matching the given base
 * address. Returns pointer to cached build ID string on success, or nullptr if no
 * matching entry is found.
 *
 * This function is async-signal-safe (no allocations, no locks, pure memory reads)
 * and safe to call from crash handlers.
 */
const char* LookupCachedBuildId(uintptr_t base_address);

}  // namespace datadog::platform
