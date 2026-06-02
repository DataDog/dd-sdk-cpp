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
import asyncio
import threading
import subprocess
from typing import TYPE_CHECKING, List

if TYPE_CHECKING:
    from lib.proxy import CapturedRequest, ProxyServer


__repo_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))

_sdk_id_lock = threading.Lock()
_sdk_id_counter = 0


def _next_sdk_id() -> int:
    global _sdk_id_counter
    with _sdk_id_lock:
        val = _sdk_id_counter
        _sdk_id_counter += 1
        return val


def _find_repl_binary() -> str:
    if sys.platform == 'win32':
        # MSVC places binaries in config-specific subdirectories; use whichever is found
        for config in ('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'):
            relpath = os.path.join('build', 'examples', config, 'repl.exe')
            abspath = os.path.join(__repo_root__, relpath)
            if os.path.isfile(abspath):
                return abspath
    else:
        # macOS/Linux builds use an unambiguous path
        abspath = os.path.join(__repo_root__, 'build', 'examples', 'repl')
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


def _normalize_input(s: str) -> str:
    lines = [line.strip() for line in s.splitlines()]
    command_lines = [line for line in lines if line and not line.startswith('#')]
    return '\n'.join(command_lines) + '\n'


class ReplProcess:
    """
    Represents a single running instance of the repl subprocess. Commands are
    written to stdin via `run()`; `join()` closes stdin, waits for the process to exit,
    and populates the result attributes.
    """
    pid: int
    storage_path: str
    requests: 'List[CapturedRequest]'
    exitcode: int
    stdout: str
    stderr: str

    def __init__(self, proc: subprocess.Popen, sdk_id: int, storage_path: str, proxy: 'ProxyServer'):
        self.pid = proc.pid
        self.storage_path = storage_path
        self._proc = proc
        self._sdk_id = sdk_id
        self._proxy = proxy
        self.requests = []
        self.exitcode = -1
        self.stdout = ''
        self.stderr = ''

    def run(self, cmds: str):
        """Writes `cmds` to the process's stdin. Returns immediately."""
        self._proc.stdin.write(_normalize_input(cmds))
        self._proc.stdin.flush()

    async def join(self) -> int:
        """
        Closes stdin (signaling EOF to the repl), waits for the process to exit, then
        populates `exitcode`, `stdout`, and `requests`. Returns the exit code.
        """
        loop = asyncio.get_event_loop()

        def _wait():
            # communicate() closes stdin (sends EOF to the repl), reads all remaining
            # stdout, and waits for the process to exit. It is safe to call after prior
            # run() writes since communicate() with no input simply closes stdin and
            # drains output.
            stdout, stderr = self._proc.communicate()
            return stdout or '', stderr or ''

        stdout_str, stderr_str = await loop.run_in_executor(None, _wait)
        self.stdout = stdout_str
        self.stderr = stderr_str
        self.exitcode = self._proc.returncode
        self.requests = self._proxy.drain(self._sdk_id)
        return self.exitcode

    def terminate(self):
        """Terminates the process if it has not been joined, then waits for it to exit."""
        if self.exitcode == -1:
            self._proc.terminate()
            self._proc.wait()
