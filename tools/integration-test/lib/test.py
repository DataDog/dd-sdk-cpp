# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Types used to define integration test cases.
"""
import os
import subprocess
import inspect
import importlib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, List, Dict

from lib.proxy import ProxyServer
from lib.repl import ReplProcess, __repo_root__, _next_sdk_id


@dataclass
class StorageDirectory:
    path: str

    def get(self, *parts: str) -> Path:
        return Path(self.path, *parts)
    
    def get_artifact_dir(self, name: str) -> Path:
        return self.get('.datadog', name)
    
    def get_pending_events_dir(self, pid: int, feature_name: str) -> Path:
        return self.get('.datadog', 'main', str(pid), feature_name, 'intermediate-v1')
    
    def get_granted_events_dir(self, pid: int, feature_name: str) -> Path:
        return self.get('.datadog', 'main', str(pid), feature_name, 'v1')


class TestContext:
    """
    Provided to each test's `main` function. Holds the shared storage directory for this
    test run and allows spawning repl processes configured to use that storage and the
    proxy.
    """
    def __init__(self, storage: StorageDirectory, repl_binary_path: str, proxy: ProxyServer, proxy_url: str):
        self.storage = storage
        self._repl_binary_path = repl_binary_path
        self._proxy = proxy
        self._proxy_url = proxy_url
        self._repls: List[ReplProcess] = []

    def spawn_repl(self) -> ReplProcess:
        """
        Spawns a new repl process configured with the test's shared storage directory
        and the proxy endpoint. The process is ready to receive commands via `run()`.
        """
        sdk_id = _next_sdk_id()
        args = [
            self._repl_binary_path,
            '--abort-on-error',
            '--abort-on-warning',
            f'--custom-endpoint-url={self._proxy_url}/sdk-{sdk_id}',
            f'--storage-path={self.storage.path}',
        ]
        proc = subprocess.Popen(
            args,
            cwd=__repo_root__,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        repl = ReplProcess(proc, sdk_id, self.storage.path, self._proxy)
        self._repls.append(repl)
        return repl


AsyncTestFunc = Callable[[TestContext], None]


@dataclass
class Test:
    name: str
    func: AsyncTestFunc


def collect_tests() -> List[Test]:
    tests: List[Test] = []
    filenames_by_test_name: Dict[str, str] = {}

    tests_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'tests'))
    for filename in sorted(os.listdir(tests_dir)):
        # Only consider Python source files
        filename_noext, ext = os.path.splitext(filename)
        if ext.lower() != '.py':
            continue

        # Skip any hidden files, __init__.py, etc.
        if filename.startswith('.') or filename.startswith('__'):
            continue

        # Import the Python file as a module and check for a symbol named 'main'
        module = importlib.import_module(f'tests.{filename_noext}')
        if 'main' not in dir(module):
            raise ValueError('No main function defined in tests/%s' % filename)

        # Verify that 'main' is an async function with the expected signature
        test_main = getattr(module, 'main')
        if not inspect.iscoroutinefunction(test_main):
            raise ValueError('main function in tests/%s must be async' % filename)
        sig = inspect.signature(test_main)
        if len(sig.parameters) != 1:
            raise ValueError('main function in tests/%s takes %d parameter(s); expected 1' % (filename, len(sig.parameters)))

        # Verify that 'main' has a docstring with a test name on the first line
        doc = inspect.getdoc(test_main)
        if not doc:
            raise ValueError('main function in tests/%s has no docstring' % filename)
        name = doc.splitlines()[0].strip()
        if not name:
            raise ValueError('main function docstring in tests/%s does not specify test name as first line' % filename)

        # Require every test to have a unique name
        other_filename = filenames_by_test_name.get(name)
        if other_filename:
            raise ValueError('duplicate test name "%s" used in both tests/%s and tests/%s' % (name, other_filename, filename))
        filenames_by_test_name[name] = filename

        tests.append(Test(name, test_main))

    return tests
