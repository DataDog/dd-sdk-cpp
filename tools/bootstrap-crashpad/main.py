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
"""
import os
import sys
import json
import platform
import subprocess
import argparse
from dataclasses import dataclass, field
from typing import Dict, List, Set

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
__crashpad_out_relpath__ = os.path.join('out', 'Default')
__crashpad_out_abspath__ = os.path.join(__crashpad_repo_root__, __crashpad_out_relpath__)


def _default_parallel_job_count() -> int:
    # For CMake builds in CI, we pass '--parallel ${NUM_PARALLEL_BUILD_JOBS}' to ensure
    # that builds don't overutilize resources, but this script is invoked from the
    # BUILD_COMMAND of a CMake ExternalProject target, which does not have access to the
    # CMAKE_BUILD_PARALLEL_LEVEL var, because the command is finalized at
    # configure-time. Therefore, in CI we can't explicitly control --parallel and must
    # instead check NUM_PARALLEL_BUILD_JOBS directly.
    num_parallel_build_jobs = os.environ.get('NUM_PARALLEL_BUILD_JOBS', '')
    if num_parallel_build_jobs:
        try:
            return int(num_parallel_build_jobs)
        except ValueError:
            pass

    # Inferring from CPU count works fine for local builds, but in CI this reflects the
    # CPU count of the host without respecting KUBERNETES_CPU_REQUEST etc.
    return os.cpu_count() - 1


def _gn_default_target_cpu():
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "x64"
    if machine in ("i386", "i686", "x86"):
        return "x86"
    if machine in ("aarch64", "arm64"):
        return "arm64"
    if machine.startswith("arm"):
        return "arm"
    raise RuntimeError(f"Unsupported architecture: {machine}")


@dataclass
class GnArgs:
    is_debug: bool = False
    target_cpu: str = _gn_default_target_cpu()
    mac_deployment_target: str = ''
    extra_cflags: Set[str] = field(default_factory=set)
    extra_cflags_c: Set[str] = field(default_factory=set)
    extra_cflags_cc: Set[str] = field(default_factory=set)
    extra_cflags_objc: Set[str] = field(default_factory=set)
    extra_cflags_objcc: Set[str] = field(default_factory=set)
    extra_ldflags: Set[str] = field(default_factory=set)
    extra_arflags: Set[str] = field(default_factory=set)

    def __str__(self) -> str:
        args = [
            f'is_debug={json.dumps(self.is_debug)}',
            f'target_cpu={json.dumps(self.target_cpu)}'
        ]
        if self.mac_deployment_target:
            args += [f'mac_deployment_target={json.dumps(self.mac_deployment_target)}']
        args += [
            f'extra_cflags={json.dumps(" ".join(sorted(self.extra_cflags)))}',
            f'extra_cflags_c={json.dumps(" ".join(sorted(self.extra_cflags_c)))}',
            f'extra_cflags_cc={json.dumps(" ".join(sorted(self.extra_cflags_cc)))}',
            f'extra_cflags_objc={json.dumps(" ".join(sorted(self.extra_cflags_objc)))}',
            f'extra_cflags_objcc={json.dumps(" ".join(sorted(self.extra_cflags_objcc)))}',
            f'extra_ldflags={json.dumps(" ".join(sorted(self.extra_ldflags)))}',
            f'extra_arflags={json.dumps(" ".join(sorted(self.extra_arflags)))}',
        ]
        return ' '.join(args)

    @classmethod
    def from_cmake(cls, vars: Dict[str, str]) -> 'GnArgs':
        args = GnArgs()

        # crashpad requires C++20; validate CMAKE_CXX_STANDARD for compatibility
        cmake_cxx_standard = vars.get('CMAKE_CXX_STANDARD', '')
        if cmake_cxx_standard and cmake_cxx_standard != '20':
            raise ValueError(f"Unsupported CMAKE_CXX_STANDARD: '{cmake_cxx_standard}'")

        # is_debug: Initialize from CMAKE_BUILD_TYPE (required)
        cmake_build_type = vars.get('CMAKE_BUILD_TYPE', '')
        if cmake_build_type == 'Debug':
            args.is_debug = True
        elif cmake_build_type in ('Release', 'RelWithDebInfo', 'MinSizeRel'):
            args.is_debug = False
        else:
            raise ValueError(f"Unsupported CMAKE_BUILD_TYPE: '{cmake_build_type}'")
        
        # target_cpu: Assume target arch is host arch (cross-compilation not supported)
        args.target_cpu = _gn_default_target_cpu()

        # mac_deployment_target: Initialize from CMAKE_OSX_DEPLOYMENT_TARGET
        if sys.platform == 'darwin':
            cmake_osx_deployment_target = vars.get('CMAKE_OSX_DEPLOYMENT_TARGET', '')
            if not cmake_osx_deployment_target:
                raise ValueError('CMAKE_OSX_DEPLOYMENT_TARGET must be specified for macOS builds')
            args.mac_deployment_target = cmake_osx_deployment_target

        # extra_cflags, extra_cflags_cc: Parse from DD_COMPILE_OPTIONS (pipe-separated);
        # filter out warnings etc.
        dd_compile_options_encoded = vars.get('DD_COMPILE_OPTIONS', '')
        dd_compile_options = {x.strip() for x in dd_compile_options_encoded.split('|') if x.strip()}
        for opt in dd_compile_options:
            if opt.startswith('-W') or opt.startswith('/W'):
                continue
            args.extra_cflags.add(opt)
            args.extra_cflags_cc.add(opt)

        # extra_ldflags: Parse from DD_LINK_OPTIONS (pipe-separated)
        dd_link_options_encoded = vars.get('DD_LINK_OPTIONS', '')
        dd_link_options = {x.strip() for x in dd_link_options_encoded.split('|') if x.strip()}
        for opt in dd_link_options:
            args.extra_ldflags.add(opt)

        # If building shared library, ensure PIC is enabled
        dd_build_shared = vars.get('DD_BUILD_SHARED', 'OFF')
        if dd_build_shared not in ('OFF', 'FALSE', '0', ''):
            args.extra_cflags.add('-fPIC')
            args.extra_cflags_cc.add('-fPIC')

        # TODO(RUM-12207): MSVC /MT, /MD, /MTd, /MDd
        # TODO(RUM-12207): Validate compiler/linker binary?

        return args


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


def fetch_crashpad():
    # If crashpad is not present, use chromium/depot_tools/fetch to clone it: note that
    # chromium/crashpad is the root directory for the build (containing metadata files
    # like `.gclient`) while chromium/crashpad/crashpad is the root of the repo
    if not os.path.isfile(os.path.join(__crashpad_build_root__, '.gclient')):
        print('Fetching crashpad...')
        os.makedirs(__crashpad_build_root__, exist_ok=True)
        _run_depot_tool(['fetch', 'crashpad'], __crashpad_build_root__)
        assert os.path.isdir(__crashpad_repo_root__)
    else:
        print('Syncing crashpad...')
        _run_depot_tool(['gclient', 'sync'], __crashpad_build_root__)
    print(f'Crashpad is present at: {__crashpad_repo_root__}')


def build_crashpad(gn_args: GnArgs, num_parallel_jobs: int):
    # depot-tools and crashpad must be present within chromium/
    assert os.path.isdir(__depot_tools_root__)
    assert os.path.isdir(__crashpad_repo_root__)

    # Use gn gen to create Ninja build files
    print('Invoking GN to generate Ninja build files for crashpad...')
    _run_depot_tool(['gn', 'gen', __crashpad_out_relpath__, f'--args={str(gn_args)}'], __crashpad_repo_root__)
    print('Invoking Ninja to build crashpad...')
    _run_depot_tool(['ninja', '-C', __crashpad_out_relpath__, '-j', str(num_parallel_jobs)], __crashpad_repo_root__)


def run_crashpad_tests():
    for binary_name in __crashpad_test_binaries__:
        binary_path = os.path.join(__crashpad_out_abspath__, binary_name)
        env = os.environ.copy()
        if sys.platform == 'win32':
            binary_path += '.exe'
            # Skip timezone tests; they don't work in Windows Docker containers
            env['GTEST_FILTER'] = '-SystemSnapshotWinTest.TimeZone'
        assert os.path.isfile(binary_path)
        print(f'Running {binary_name}...')
        subprocess.check_call([binary_path], env=env)
    print('All crashpad tests passed.')


def install_main(args: argparse.Namespace):
    clone_depot_tools()
    fetch_crashpad()


def gn_args_main(args: argparse.Namespace):
    _run_depot_tool(['gn', 'args', __crashpad_out_relpath__, '--list'], __crashpad_repo_root__)


def build_main(args: argparse.Namespace):
    # Seamlessly install tools and sync the crashpad repo unless instructed not to
    if not args.no_install:
        clone_depot_tools()
        fetch_crashpad()

    # Parse '-c FOO=ON -c BAR=bar' into {'FOO': 'ON', 'BAR': 'bar'}
    cmake_vars: Dict[str, str] = {}
    for s in args.cmake_vars:
        if '=' not in s:
            raise ValueError(f"Unexpected format for CMake var: '{s}'")
        k, v = s.split('=', 1)
        cmake_vars[k] = v

    # If we've been provided with any CMake args, resolve the corresponding set of gn
    # args to ensure that our crashpad build is binary-compatible with our CMake build
    gn_args = GnArgs()
    if cmake_vars:
        gn_args = GnArgs.from_cmake(cmake_vars)

    # Use gn and ninja to produce a build of crashpad
    build_crashpad(gn_args, args.parallel)

    # Run crashpad tests to validate the build, unless instructed not to
    if not args.no_test:
        run_crashpad_tests()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest='command', required=True)

    install_parser = subparsers.add_parser('install')
    install_parser.set_defaults(func=install_main)

    gn_args_parser = subparsers.add_parser('gn-args')
    gn_args_parser.set_defaults(func=gn_args_main)

    build_parser = subparsers.add_parser('build')
    build_parser.add_argument('--no-install', '-I', action='store_true', help='If set, skip install/sync of crashpad and depot_tools')
    build_parser.add_argument('--no-test', '-T', action='store_true', help='If set, skip running crashpad tests post-build')
    build_parser.add_argument('--cmake-var', '-c', action='append', dest='cmake_vars', default=[], help='CMake var in KEY=VALUE format; used to configure GN build')
    build_parser.add_argument('--parallel', '-j', type=int, default=_default_parallel_job_count(), help='Number of parallel build jobs to use when invoking ninja')
    build_parser.set_defaults(func=build_main)

    args = parser.parse_args()
    args.func(args)
