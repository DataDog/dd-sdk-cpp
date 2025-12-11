# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Helper functions for resolving configuration details.
"""
import sys
import datetime
from typing import List

from lib.cmake import load_cmake_cache


def auto_resolve_revision_name() -> str:
    return datetime.datetime.now().strftime('%x %X')


def auto_resolve_platform() -> str:
    return sys.platform


def auto_resolve_build_config() -> str:
    # Assume that the SDK and repl have been built locally from a CMake configuration
    # generated to build/
    cmake_cache = load_cmake_cache()
    if not cmake_cache:
        raise RuntimeError('Unable to auto-resolve build config: no build/CMakeCache.txt file found')

    # Infer from CMAKE_BUILD_TYPE, which informs optimization level for C++ compiles
    # (Debug is -g, Release is -O3, RelWithDebInfo is -O2)
    cmake_build_type = cmake_cache.get('CMAKE_BUILD_TYPE')
    if cmake_build_type not in ('Debug', 'Release', 'RelWithDebInfo'):
        raise RuntimeError(f'Unable to auto-resolve build config from CMAKE_BUILD_TYPE {cmake_build_type}')

    # Check CMake options that meaningfully impact performance: for consistency, we only
    # permit these to be enabled in a 'development' config
    development_flags = ['DD_ENABLE_ASSERTS', 'DD_ENABLE_COVERAGE', 'DD_ENABLE_SANITIZERS']
    enabled_development_flags: List[str] = []
    for flag in development_flags:
        if cmake_cache.has_flag(flag):
            enabled_development_flags.append(flag)

    # 'development' is an unoptimized build with asserts/sanitizers/etc. enabled
    if cmake_build_type == 'Debug' and enabled_development_flags == development_flags:
        return 'development'

    # 'debug' is an unoptimized build without extra development flags
    if cmake_build_type == 'Debug' and not enabled_development_flags:
        return 'debug'

    # 'test' is a release build with debug info, -O2 instead of -O3
    if cmake_build_type == 'RelWithDebInfo' and not enabled_development_flags:
        return 'test'

    # 'release' is a fully-optimized release build
    if cmake_build_type == 'Release' and not enabled_development_flags:
        return 'release'

    # Any other combination of development flags is not supported
    raise RuntimeError(f'Unable to auto-resolve build config with CMAKE_BUILD_TYPE {cmake_build_type} and {", ".join(enabled_development_flags)}')
