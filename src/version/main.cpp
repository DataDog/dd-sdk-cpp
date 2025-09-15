// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include <cinttypes>
#include <cstring>
#include <iostream>

#include "core/version.hpp"

/**
 * This program is an internal tool used in the release process: it prints information
 * about the build environment in which the library was compiled, so that we can
 * affirmatively identify release packages based on platform, architecture, compiler
 * toolchain, SDK version, etc.
 *
 * Examples:
 * > ./dd_native_version
 * 0.2.0
 * > ./dd_native_version artifact
 * ddsdkcpp-v0.2.0-macos-arm64-clang-libc++-static-release
 */
int main(int argc, char* argv[]) {
  // Usage: dd_native_version [-e] [artifact|version]
  int i = 1;

  // Behave like 'echo': print newline by default, supress if called with -e
  const char* lf = "\n";
  if (argc > i && std::strcmp(argv[i], "-e") == 0) {
    lf = "";
    i++;
  }

  // Default to printing the version number, but allow a positional arg to override
  std::string_view value = datadog::impl::SDK_VERSION;
  if (argc > i && std::strcmp(argv[i], "artifact") == 0) {
    value = datadog::impl::BUILD_ARTIFACT_NAME;
  }

  // Print to stdout
  std::cout << value << lf;
  return 0;
}
