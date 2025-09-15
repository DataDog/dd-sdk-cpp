// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

#if __cplusplus >= 202002L
#include <version>
#else
#include <ciso646>
#endif

// This file defines WITH_DATADOG_STRICT_MEMORY_CHECKS, which influences whether we're
// building our tests in an environment that will allow us to perform strict assertions
// on the expected heap allocation behavior of our code.

// Our code's exact heap allocation behavior depends on compiler-specific implementation
// details, such as std::string's small string optimization (SSO), or the default
// allocation/growth strategies of STL containers like std::vector. The build
// environment in which we author our tests is the "golden path": if we're running tests
// in the same environment where we dialed in the expected values for allocation counts
// and sizes, then we can assert on those expected values strictly. If we detect that
// we're building in this "golden path" environment, WITH_DATADOG_STRICT_MEMORY_CHECKS
// will default to enabled.

// On all other platforms, where our tests may need to be less stringent,
// WITH_DATADOG_STRICT_MEMORY_CHECKS will default to off.

// Require 64-bit builds with the LP64 data model
#if defined(__LP64__)
#define DATADOG_SMC_LP64 1
#else
#define DATADOG_SMC_LP64 0
#endif

// Require a specific major version of libc++, which matches LLVM version
#if defined(_LIBCPP_VERSION)
#define DATADOG_SMC_LIBCPP_MAJOR (_LIBCPP_VERSION / 10000)
#else
#define DATADOG_SMC_LIBCPP_MAJOR 0
#endif

// Require a specific libc++ ABI version (1 is stable)
#if defined(_LIBCPP_ABI_VERSION)
#define DATADOG_SMC_LIBCPP_ABI_VERSION _LIBCPP_ABI_VERSION
#else
#define DATADOG_SMC_LIBCPP_ABI_VERSION 0
#endif

// Require that iterator debugging be disabled
#if defined(_LIBCPP_DEBUG)
#define DATADOG_SMC_STL_DEBUG 1
#else
#define DATADOG_SMC_STL_DEBUG 0
#endif

// Require that sanitizers be disabled
#if defined(__has_feature)
#define DATADOG_SMC_ASAN __has_feature(address_sanitizer)
#define DATADOG_SMC_MSAN __has_feature(memory_sanitizer)
#define DATADOG_SMC_TSAN __has_feature(thread_sanitizer)
#else
#define DATADOG_SMC_ASAN 0
#define DATADOG_SMC_MSAN 0
#define DATADOG_SMC_TSAN 0
#endif

// Define the golden-path build environment in which we author tests
// - Must be a 64-bit build
// - Must target libc++ 19 or newer
// - Must use libc++ ABI version 1
// - Must have no STL iterator debugging and no sanitizers
#define DATADOG_SMC_GOLDEN_PATH                                             \
  ((DATADOG_SMC_LP64 == 1) && (DATADOG_SMC_LIBCPP_MAJOR >= 19) &&           \
   (DATADOG_SMC_LIBCPP_ABI_VERSION == 1) && (DATADOG_SMC_STL_DEBUG == 0) && \
   (DATADOG_SMC_ASAN == 0) && (DATADOG_SMC_MSAN == 0) && (DATADOG_SMC_TSAN == 0))

// If WITH_DATADOG_STRICT_MEMORY_CHECKS was not explicitly defined for the build, define
// it as 1 if we're on the golden path; 0 otherwise
#ifndef WITH_DATADOG_STRICT_MEMORY_CHECKS
#define WITH_DATADOG_STRICT_MEMORY_CHECKS DATADOG_SMC_GOLDEN_PATH
#endif

// If strict memory checks are enabled but we're not on the auto-detected golden path,
// dump our computed configuration and fail the build
#if WITH_DATADOG_STRICT_MEMORY_CHECKS && !DATADOG_SMC_GOLDEN_PATH
#define DATADOG_SMC_STRINGIFY(x) #x
#define DATADOG_SMC_TOSTRING(x) DATADOG_SMC_STRINGIFY(x)
#pragma message(                                                                                                                                                                                                    \
    "Build environment: LP64=" DATADOG_SMC_TOSTRING(DATADOG_SMC_LP64) ", LIBCPP_MAJOR=" DATADOG_SMC_TOSTRING(DATADOG_SMC_LIBCPP_MAJOR) ", LIBCPP_ABI_VERSION=" DATADOG_SMC_TOSTRING(                                \
        DATADOG_SMC_LIBCPP_ABI_VERSION                                                                                                                                                                              \
    ) ", STL_DEBUG=" DATADOG_SMC_TOSTRING(DATADOG_SMC_STL_DEBUG) ", ASAN=" DATADOG_SMC_TOSTRING(DATADOG_SMC_ASAN) ", MSAN=" DATADOG_SMC_TOSTRING(DATADOG_SMC_MSAN) ", TSAN=" DATADOG_SMC_TOSTRING(DATADOG_SMC_TSAN) \
)
#error \
    "WITH_DATADOG_STRICT_MEMORY_CHECKS is enabled, but build environment differs from golden path"
#endif
