// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

namespace datadog::impl {
class IFilesystem;
class PlatformPath;
struct CrashContext;
}  // namespace datadog::impl

namespace datadog::impl {

/**
 * Creates or overwrites a crash context file at `path`, serializing `ctx` in the v1
 * binary format. Uses a write-then-rename pattern, first flushing the complete file to
 * `tmp_path` before atomically replacing the original.
 *
 * The file persists across crashes so the next SDK launch can recover the full SDK
 * state and attach it to any crash report found on disk.
 *
 * On success, returns true. On failure, returns false and leaves any pre-existing file
 * at `path` untouched.
 */
bool WriteCrashContext(
    IFilesystem& fs,
    const PlatformPath& path,
    const PlatformPath& tmp_path,
    const CrashContext& ctx
);

}  // namespace datadog::impl
