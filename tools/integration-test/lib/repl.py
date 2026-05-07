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
from dataclasses import dataclass


__repo_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
__repl_binary__ = os.path.join(__repo_root__, 'build', 'examples', 'dd_native_repl')
if sys.platform == 'win32':
    __repl_binary__ += '.exe'


@dataclass
class ReplResult:
    exitcode: int
    stdout: str
    stderr: str

    @property
    def ok(self) -> bool:
        return self.exitcode == 0 and not self.stderr


def check_repl_binary():
    if not os.path.isfile(__repl_binary__):
        print('ERROR: repl binary not found at: %s' % __repl_binary__)
        print('Running integration tests requires a valid CMake build with DD_BUILD_EXAMPLES enabled.')
        print('Reconfigure with -DDD_BUILD_EXAMPLES=ON (or -DDD_DEVELOPMENT=ON) and run cmake --build build.')
        sys.exit(1)


def run_repl(storage_path: str, custom_endpoint_url: str, sdk_id: int, script: str) -> ReplResult:
    args = [
        __repl_binary__,
        '--abort-on-error',
        '--abort-on-warning',
        f'--storage-path={storage_path}',
        f'--custom-endpoint-url={custom_endpoint_url}/sdk-{sdk_id}'
    ]
    p = subprocess.Popen(args, cwd=__repo_root__, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    stdout, stderr = p.communicate(script)
    return ReplResult(p.returncode, stdout, stderr)
