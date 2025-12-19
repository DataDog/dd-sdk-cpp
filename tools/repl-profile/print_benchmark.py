# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
repl-profile/apply_benchmark_results.py
Usage: python3 tools/repl-profile/apply_benchmark_results.py
Requires no external dependencies; any Python 3.10+ interpreter will work.

Prints the text of a single benchmark script, suitable for piping into a repl process.
Supply '--mode cpu' or '--mode memory' to inject the necessary commands to enable
profiling and parse profile output.
"""
import sys
import argparse

from lib.benchmark import collect_benchmarks


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('name', help='Name of the benchmark to print')
    parser.add_argument('--mode', choices=['none', 'cpu', 'memory'], default='none', help='Mode to use for start-profile command. If none, no profiling commands will be injected.')
    args = parser.parse_args()

    benchmarks = collect_benchmarks()
    benchmark = next((b for b in benchmarks if b.name == args.name), None)
    if not benchmark:
        print(f'ERROR: No benchmark found with name "{args.name}"')
        sys.exit(1)

    print(benchmark.render(args.mode))
