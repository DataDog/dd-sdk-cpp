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


@dataclass
class ReplResult:
    exitcode: int
    stdout: str
    stderr: str

    @property
    def ok(self) -> bool:
        return self.exitcode == 0 and not self.stderr


def _find_repl_binary() -> str:
    if sys.platform == 'win32':
        # MSVC places binaries in config-specific subdirectories; use whichever is found
        for config in ('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'):
            relpath = os.path.join('build', 'examples', config, 'dd_native_repl.exe')
            abspath = os.path.join(__repo_root__, relpath)
            if os.path.isfile(abspath):
                return abspath
    else:
        # macOS/Linux builds use an unambiguous path
        abspath = os.path.join(__repo_root__, 'build', 'examples', 'dd_native_repl')
        if os.path.isfile(abspath):
            return abspath
        
    return ''


def check_repl_binary() -> str:
    repl_binary_path = _find_repl_binary()
    if not repl_binary_path:
        print('ERROR: repl binary not found within: %s' % __repo_root__)
        print('Running integration tests requires a valid CMake build with DD_BUILD_EXAMPLES enabled.')
        print('Reconfigure with -DDD_BUILD_EXAMPLES=ON (or -DDD_DEVELOPMENT=ON) and run cmake --build build.')
        sys.exit(1)
    return repl_binary_path


def run_repl(repl_binary_path: str, storage_path: str, custom_endpoint_url: str, sdk_id: int, script: str) -> ReplResult:
    args = [
        repl_binary_path,
        '--abort-on-error',
        '--abort-on-warning',
        f'--storage-path={storage_path}',
        f'--custom-endpoint-url={custom_endpoint_url}/sdk-{sdk_id}'
    ]
    p = subprocess.Popen(args, cwd=__repo_root__, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    stdout, stderr = p.communicate(script)
    return ReplResult(p.returncode, stdout, stderr)
