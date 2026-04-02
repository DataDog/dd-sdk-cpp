// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

namespace datadog::impl {
struct RumFeatureContext;
}

namespace datadog::platform {

/**
 * Creates or overwrites a crash context file at `filename`, writing the current RUM
 * session identifiers in binary format.
 *
 * The file persists across crashes so the next SDK launch can recover the session
 * context and attach it to any crash report found on disk. Call `DeleteCrashContext` on
 * clean shutdown to remove it.
 */
void WriteCrashContext(const char* filename, const impl::RumFeatureContext& rum_ctx);

/**
 * Deletes the crash context file at `filename`, if any such file exists.
 */
void DeleteCrashContext(const char* filename);

}  // namespace datadog::platform
