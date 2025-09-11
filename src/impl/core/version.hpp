#pragma once

#include <string_view>

#if __cplusplus >= 202002L
#include <version>
#else
#include <ciso646>
#endif

// Library details used to label a release
#define DATADOG_BUILD_LIBRARY_NAME "ddsdkcpp"
#ifndef DATADOG_BUILD_VERSION
#define DATADOG_BUILD_VERSION "0.0.0"
#endif

// platform: e.g. 'windows', 'macos', 'linux'
#if defined(_WIN32)
#define DATADOG_BUILD_PLATFORM "windows"
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define DATADOG_BUILD_PLATFORM "unsupported-ios"
#else
#define DATADOG_BUILD_PLATFORM "macos"
#endif
#elif defined(__ANDROID__)
#define DATADOG_BUILD_PLATFORM "unsupported-android"
#elif defined(__linux__)
#define DATADOG_BUILD_PLATFORM "linux"
#else
#define DATADOB_BUILD_PLATFORM "unsupported-platform"
#endif

// arch: e.g. 'x64', 'arm64'
#if defined(__x86_64__) || defined(_M_X64)
#define DATADOG_BUILD_ARCH "x64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define DATADOG_BUILD_ARCH "arm64"
#else
#define DATADOG_BUILD_ARCH "unsupported-arch"
#endif

// compiler: Windows uses MSVC version, Unix uses stdlib implementation
#if defined(_MSC_VER)
#if _MSC_VER >= 1930
#define DATADOG_BUILD_COMPILER "msvc143"  // VS 2022
#elif _MSC_VER >= 1920
#define DATADOG_BUILD_COMPILER "msvc142"  // VS 2019
#elif _MSC_VER >= 1910
#define DATADOG_BUILD_COMPILER "msvc141"  // VS 2017
#elif _MSC_VER >= 1900
#define DATADOG_BUILD_COMPILER "msvc140"  // VS 2015
#else
#define DATADOG_BUILD_COMPILER "unsupported-compiler"
#endif
#elif defined(__clang__)
#define DATADOG_BUILD_COMPILER "clang"
#elif defined(__GNUC__)
#define DATADOG_BUILD_COMPILER "gcc"
#else
#define DATADOG_BUILD_COMPILER "unsupported-compiler"
#endif

// stdlib: 'libc++' or 'libstdc++' on POSIX; CRT linkage ('md' or 'mt') on Windows
#if defined(_WIN32)
#if defined(_MT) && defined(_DLL)
#define DATADOG_BUILD_STDLIB "md"  // Dynamic CRT
#elif defined(_MT)
#define DATADOG_BUILD_STDLIB "mt"  // Static CRT
#else
#define DATADOG_BUILD_STDLIB "unsupported-crt"
#endif
#elif defined(_LIBCPP_VERSION)
#define DATADOG_BUILD_STDLIB "libc++"
#elif defined(__GLIBCXX__)
#define DATADOG_BUILD_STDLIB "libstdc++"
#else
#define DATADOG_BUILD_STDLIB "unsupported-stdlib"
#endif

// linkage: e.g. 'static', 'shared'
#ifdef DATADOG_SHARED_LIB
#define DATADOG_BUILD_LINKAGE "shared"
#else
#define DATADOG_BUILD_LINKAGE "static"
#endif

// variant: e.g. 'debug', 'release'
#ifdef NDEBUG
#define DATADOG_BUILD_VARIANT "release"
#else
#define DATADOG_BUILD_VARIANT "debug"
#endif

// Build artifact name:
// '{library}-v{version}-{platform}-{arch}-{compiler}-{stdlib}-{linkage}-{variant}'
#define DATADOG_BUILD_ARTIFACT_NAME                                                \
  DATADOG_BUILD_LIBRARY_NAME "-v" DATADOG_BUILD_VERSION "-" DATADOG_BUILD_PLATFORM \
                             "-" DATADOG_BUILD_ARCH "-" DATADOG_BUILD_COMPILER     \
                             "-" DATADOG_BUILD_STDLIB "-" DATADOG_BUILD_LINKAGE    \
                             "-" DATADOG_BUILD_VARIANT

namespace datadog::impl {

static const std::string_view SDK_VERSION = DATADOG_BUILD_VERSION;
static const std::string_view BUILD_ARTIFACT_NAME = DATADOG_BUILD_ARTIFACT_NAME;

}  // namespace datadog::impl
