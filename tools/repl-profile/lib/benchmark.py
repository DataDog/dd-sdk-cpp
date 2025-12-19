# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for collecting repl scripts to be run as benchmarks.
"""
import os
import importlib
from dataclasses import dataclass
from typing import List, Optional


@dataclass
class Benchmark:
    name: str
    setup: str
    instrumented: str
    teardown: str

    def render(self, mode: Optional[str]) -> str:
        if mode and mode != 'none':
            lines = (
                # Begin profiling repl execution from the very start, then run the benchmark's
                # setup commands, collecting running totals
                [f'start-profile {mode}'] +
                self.setup +
                # Run the benchmark script's instrumented commands, using magic labels to
                # indicate to the parser that we want to collect granular stats here
                ['nop "BEGIN PROFILING"'] +
                self.instrumented +
                ['nop "END PROFILING"'] +
                # Run the teardown script, then stop profiling and exit
                self.teardown +
                ['stop-profile']
            )
        else:
            lines = self.setup + self.instrumented + self.teardown
        return '\n'.join(lines) + '\n'


def collect_benchmarks() -> List[Benchmark]:
    benchmarks: List[Benchmark] = []
    benchmarks_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'benchmarks'))
    for filename in sorted(os.listdir(benchmarks_dir)):
        # Only consider Python source files
        filename_noext, ext = os.path.splitext(filename)
        if ext.lower() != '.py':
            continue

        # Skip any hidden files, __init__.py, etc.
        if filename.startswith('.') or filename.startswith('__'):
            continue

        # Import the Python file as a module and check for a symbol named 'main'
        module = importlib.import_module(f'benchmarks.{filename_noext}')
        if 'INSTRUMENTED' not in dir(module):
            raise ValueError('No INSTRUMENTED string defined in benchmarks/%s' % filename)
        
        # Verify that 'INSTRUMENTED' is a string
        instrumented = getattr(module, 'INSTRUMENTED')
        if not isinstance(instrumented, str):
            raise ValueError('INSTRUMENTED value defined in benchmarks/%s is not a string' % filename)
        
        # Check for optional 'SETUP' and 'TEARDOWN' values, which must be strings if set
        setup = getattr(module, 'SETUP') if 'SETUP' in dir(module) else ''
        if not isinstance(setup, str):
            raise ValueError('SETUP value defined in benchmarks/%s is not a string' % filename)
        teardown = getattr(module, 'TEARDOWN') if 'TEARDOWN' in dir(module) else ''
        if not isinstance(teardown, str):
            raise ValueError('TEARDOWN value defined in benchmarks/%s is not a string' % filename)

        # Construct a Benchmark object and add it to our list
        benchmarks.append(Benchmark(
            filename_noext,
            _read_script_lines(setup),
            _read_script_lines(instrumented),
            _read_script_lines(teardown),
        ))

    # Return our accumulated list of Benchmark objects
    return benchmarks


def _read_script_lines(s: str) -> List[str]:
    stripped_lines = [line.strip() for line in s.splitlines()]
    return [line for line in stripped_lines if line and not line.startswith('#')]
