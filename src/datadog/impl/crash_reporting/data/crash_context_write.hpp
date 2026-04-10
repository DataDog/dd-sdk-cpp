// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

namespace datadog::impl {
class IFilesystem;
class PlatformPath;
struct RumFeatureContext;
}  // namespace datadog::impl

namespace datadog::impl {

/**
 * Creates or overwrites a crash context file at `path`, writing the current RUM session
 * identifiers in binary format. Uses a write-then-rename pattern, first flushing the
 * complete file to `tmp_path` before replacing the original file.
 *
 * The file persists across crashes so the next SDK launch can recover the session
 * context and attach it to any crash report found on disk.
 */
bool WriteCrashContext(
    IFilesystem& fs,
    const PlatformPath& path,
    const PlatformPath& tmp_path,
    const RumFeatureContext& rum_ctx
);

}  // namespace datadog::impl
