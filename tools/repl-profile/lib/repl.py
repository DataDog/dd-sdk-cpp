# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Utility code for running the dd-sdk-cpp repl process.
"""
import os
import sys
import subprocess

from lib.benchmark import Benchmark
from lib.profile import Profile


__repo_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
__repl_binary__ = os.path.join(__repo_root__, 'build', 'examples', 'dd_native_repl')
if sys.platform == 'win32':
    __repl_binary__ += '.exe'


def check_repl_binary():
    if not os.path.isfile(__repl_binary__):
        print('ERROR: repl binary not found at: %s' % __repl_binary__)
        print('Running benchmarks requires a valid CMake build with DD_BUILD_EXAMPLES enabled.')
        print('Reconfigure with -DDD_BUILD_EXAMPLES=ON (or -DDD_DEVELOPMENT=ON) and run cmake --build build.')
        sys.exit(1)


def run_repl(benchmark: Benchmark, custom_endpoint_url: str, mode: str) -> Profile:
    # Prepare the repl script that will run our chosen benchmark with profiling
    if mode not in ('cpu', 'memory'):
        raise ValueError(f'Invalid profiling mode {mode}')
    script = benchmark.render(mode)
    args = [
        __repl_binary__,
        '--abort-on-error',
        '--abort-on-warning',
        f'--custom-endpoint-url={custom_endpoint_url}'
    ]
    p = subprocess.Popen(args, cwd=__repo_root__, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    stdout, stderr = p.communicate(script)
    if p.returncode != 0:
        print(stderr)
        print(stdout)
        raise RuntimeError(f'repl exited with status code {p.returncode}')
    profile = Profile()
    for line in stdout.splitlines():
        profile.read(line)
    return profile
