// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_VERSION_H
#define DATADOG_INCLUDE_VERSION_H

#include "datadog/api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Exposes basic information about the revision of the Datadog C++ SDK that's in use for
 * the current application. All members are guaranteed to be valid, null-terminated
 * strings with static storage.
 */
typedef struct dd_version_info {
  /**
   * The most recent SDK version as of this build, e.g. '0.2.0'. If unknown, defaults to
   * '0.0.0'. Derived from the CMake project version.
   */
  const char* release;
  /**
   * A short identifier describing the git revision from which the SDK library was
   * built, e.g. 'main-971cf71' or 'rum13213-f6858e4-3ec13ac'. If unknown, defaults to
   * 'unknown'.
   */
  const char* revision_id;
  /**
   * An identifier describing the build configuration used to produce the library, e.g.
   * 'ddsdkcpp-v0.2.0-linux-x64-clang-libstdc++-shared-release'.
   */
  const char* artifact_name;
} dd_version_info_t;

/**
 * Returns the version information stamped into this build of the Datadog SDK.
 *
 * This function is provided for internal, diagnostic usage; it may change or be removed
 * at any time.
 */
DATADOG_API dd_version_info_t dd_get_version_info(void);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_VERSION_H
