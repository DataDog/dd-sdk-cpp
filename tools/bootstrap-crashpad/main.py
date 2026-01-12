# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
bootstrap-crashpad
Usage: python3 tools/bootstrap-crashpad/main.py

This script is used to pull crashpad into the build, placing its source tree at
chromium/crashpad/crashpad and building it to chromium/crashpad/crashpad/out/Default.

As part of the Chromium project, Crashpad uses `BUILD.gn` files to describe its build
configuration. Those files are fed into GN in order to generate Ninja build files. For
more information, see:

https://chromium.googlesource.com/crashpad/crashpad/+/HEAD/doc/developing.md

- TODO(RUM-12207): Integration into CMake build, enough to ensure that the crashpad
  binaries produced are compatible with the build of dd-sdk-cpp as configured
- TODO(RUM-12207): Versioning of gclient metadata to pin target version(s)
- TODO(RUM-12207): Command-line control for different commands (upgrading to latest
  upstream crashpad version vs. building vs. running tests)
"""
import os
import sys
import subprocess
import argparse
from typing import Dict, List

__repo_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
__chromium_root__ = os.path.join(__repo_root__, 'chromium')
__depot_tools_root__ = os.path.join(__chromium_root__, 'depot_tools')
__crashpad_build_root__ = os.path.join(__chromium_root__, 'crashpad')
__crashpad_repo_root__ = os.path.join(__crashpad_build_root__, 'crashpad')
__crashpad_test_binaries__ = [
    'crashpad_client_test',
    'crashpad_handler_test',
    'crashpad_minidump_test',
    'crashpad_snapshot_test',
    'crashpad_test_test',
    'crashpad_util_test',
]


def _depot_tools_env() -> Dict[str, str]:
    env = os.environ.copy()
    env['PATH'] = __depot_tools_root__ + os.pathsep + env['PATH']
    return env


def _run_depot_tool(args: List[str], cwd: str) -> int:
    if sys.platform == 'win32':
        args[0] = os.path.join(__depot_tools_root__, args[0] + '.bat')
    return subprocess.check_call(args, env=_depot_tools_env(), cwd=cwd)


def clone_depot_tools():
    # If needed, clone into chromium/depot_tools so we can use 'fetch' and related tools
    # to pull down Chromium-project source repos
    os.makedirs(__chromium_root__, exist_ok=True)
    if not os.path.isdir(__depot_tools_root__):
        print('Cloning depot_tools...')
        subprocess.check_call(['git', 'clone', 'https://chromium.googlesource.com/chromium/tools/depot_tools.git'], cwd=__chromium_root__)
        assert os.path.isdir(__depot_tools_root__)
    print(f'depot_tools present at: {__depot_tools_root__}')



def fetch_crashpad(update=True):
    # If crashpad is not present, use chromium/depot_tools/fetch to clone it: note that
    # chromium/crashpad is the root directory for the build (containing metadata files
    # like `.gclient`) while chromium/crashpad/crashpad is the root of the repo
    if not os.path.isdir(__crashpad_repo_root__):
        print('Fetching crashpad...')
        os.makedirs(__crashpad_build_root__, exist_ok=True)
        _run_depot_tool(['fetch', 'crashpad'], __crashpad_build_root__)
        assert os.path.isdir(__crashpad_repo_root__)
    elif update:
        _run_depot_tool(['gclient', 'sync'], __crashpad_repo_root__)
    print(f'Crashpad is present at: {__crashpad_repo_root__}')


def build_crashpad(num_parallel_jobs: int):
    # depot-tools and crashpad must be present within chromium/
    assert os.path.isdir(__depot_tools_root__)
    assert os.path.isdir(__crashpad_repo_root__)

    # Use gn gen to create Ninja build files
    relpath = os.path.join('out', 'Default')
    _run_depot_tool(['gn', 'gen', relpath], __crashpad_repo_root__)
    _run_depot_tool(['ninja', '-C', relpath, '-j', str(num_parallel_jobs)], __crashpad_repo_root__)


def run_crashpad_tests():
    for binary_name in __crashpad_test_binaries__:
        binary_path = os.path.join(__crashpad_repo_root__, 'out', 'Default', binary_name)
        if sys.platform == 'win32':
            binary_path += '.exe'
        assert os.path.isfile(binary_path)
        print(f'Running {binary_name}...')
        subprocess.check_call([binary_path])
    print('All crashpad tests passed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--parallel', '-j', type=int, default=os.cpu_count() - 1, help='Number of parallel build jobs to use when invoking ninja')
    args = parser.parse_args()

    clone_depot_tools()
    fetch_crashpad(update=True)
    build_crashpad(num_parallel_jobs=args.parallel)
    run_crashpad_tests()
