# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
repl-profile/run_benchmarks.py
Usage: python3 tools/repl-profile/run_benchmarks.py
Requires no external dependencies; any Python 3.10+ interpreter will work.

Uses the repl binary (which must exist in examples/repl) to run all benchmarks defined
for the C++ SDK, with profiling enabled, parsing output and aggregating profiling data
so it can be stored in a SQLite database, benchmark-results.db.

See tools/repl-profile/benchmarks/ for the definition of each benchmark.

To profile an optimized release build, configure with `-DCMAKE_BUILD_TYPE=Release` and
`-DDD_BUILD_EXAMPLES=ON`, then run `cmake --build build [--target repl]`. This
script will examine your CMakeCache.txt file to ascertain the build configuration it's
profiling.

By default, benchmark results will be written directly to
`tools/repl-profile/benchmark-results.db`. Supply `-o results.sql` to write the data to
a SQL file instead: you can then use `apply_benchmark_results.py` to merge multiple .sql
files into database at once.
"""
import sys
import argparse
from typing import Dict

from db.connection import db_connect
from db.migrations import db_migrate
from db.models import (
    BenchmarkResults,
    BenchmarkInvocation,
    CommandExecution,
    AllocEvent,
    AggregateDuration,
)
from db.queries import store_benchmark_results

from lib.config import auto_resolve_revision_name, auto_resolve_build_config, auto_resolve_platform
from lib.benchmark import collect_benchmarks
from lib.repl import check_repl_binary, run_repl
from lib.server import HTTPServer


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--revision', default='auto', help='Name of the SDK revision for which results should be recorded. Omit to use an auto-generated name based on the current time.')
    parser.add_argument('--platform', default='auto', help='Name of the platform for which results should be recorded. Omit to resolve from sys.platform.')
    parser.add_argument('--config', default='auto', help='Name of the build configuration used to build the SDK and repl. Omit to infer from CMakeCache.txt if possible.')
    parser.add_argument('--num-cpu-samples', '-n', type=int, default=10, help='Number of CPU-profiling iterations to run in order to account for statistical variance.')
    parser.add_argument('--output', '-o', help='Path to the .sql file to write profiling data to. If unset, data will be written directly to benchmark-results.db.')
    args = parser.parse_args()

    print('Running benchmarks to gather profiling data...')

    # Verify that we have a repl binary in build/
    check_repl_binary()

    # Import all benchmark scripts defined in benchmarks/
    benchmarks = collect_benchmarks()
    if not benchmarks:
        print('ERROR: No benchmarks found')
        sys.exit(1)

    # Resolve the details of the C++ SDK revision we're testing and the environment
    # we're testing it in
    revision_name = auto_resolve_revision_name() if args.revision == 'auto' else args.revision
    platform = auto_resolve_platform() if args.platform == 'auto' else args.platform
    build_config = auto_resolve_build_config() if args.config == 'auto' else args.config
    print(f'revision_name: {revision_name}')
    print(f'platform: {platform}')
    print(f'build_config: {build_config}')

    # If we're configured to modify the local SQLite database instead of dumping out a
    # SQL script, ensure that we have benchmark-results.db with all migrations applied
    if not args.output:
        db_migrate()

    # Run a dummy HTTP server so the SDK will be able to upload data
    server = HTTPServer()
    server.start()
    print(f'HTTP server listening on port {server.port}...')
    custom_endpoint_url = f'http://127.0.0.1:{server.port}'

    # Run all of our benchmark scripts in the repl binary
    print(f'Running {len(benchmarks)} benchmark scripts...')
    try:
        invocations: Dict[str, BenchmarkInvocation] = {}
        for benchmark in benchmarks:
            # Run once to get detailed memory allocation stats, with overinflated CPU
            # time stats due to memory profiling overhead
            print(f'- {benchmark.name}')
            print('  - [memory] invocation 1 of 1')
            memory_profile = run_repl(benchmark, custom_endpoint_url, 'memory')

            # We expect every repl invocation with the same script to run the same
            # sequence of commands: store the list of labels (parsed from
            # '< Logger::Log()' etc.) seen in this first run
            command_seq = [c.label for c in memory_profile.commands]

            # Run the same benchmark N times, with CPU profiling only, so we can get
            # enough samples for setup duration, teardown duration, and per-command
            # duration to compensate for statistical variance
            duration_num_samples = args.num_cpu_samples
            setup_duration_samples = [None] * duration_num_samples
            command_duration_samples = [[None] * duration_num_samples for _ in range(len(command_seq))]
            teardown_duration_samples = [None] * duration_num_samples
            for sample_index in range(duration_num_samples):
                print(f'  - [cpu] invocation {sample_index + 1} of {duration_num_samples}')
                cpu_profile = run_repl(benchmark, custom_endpoint_url, 'cpu')

                # Require that this invocation produced the exact same sequence of
                # commands as all previous invocations, since we plan to aggregate data
                # on a command-by-command basis
                if [c.label for c in cpu_profile.commands] != command_seq:
                    print('expected: %r' % command_seq)
                    print('got: %r' % [c.label for c in cpu_profile.commands])
                    raise RuntimeError(f'Benchmark {benchmark.name} yielded a different set of commands across invocations')

                # Accumulate duration samples
                setup_duration_samples[sample_index] = cpu_profile.setup_duration_ns / 1e9
                teardown_duration_samples[sample_index] = cpu_profile.teardown_duration_ns / 1e9
                for command_index, command in enumerate(cpu_profile.commands):
                    command_duration_samples[command_index][sample_index] = command.duration_ns / 1e9

            # Aggregate our CPU time stats
            setup_duration = AggregateDuration.compute(setup_duration_samples)
            teardown_duration = AggregateDuration.compute(teardown_duration_samples)

            # Build a list of CommandExecution objects, each of which has memory stats
            # from our memory-profiling run and CPU stats aggregated from our multiple
            # CPU-profiling runs
            commands = [None] * len(command_seq)
            for command_index, command_mem in enumerate(memory_profile.commands):
                commands[command_index] = CommandExecution(
                    command_mem.label,
                    AggregateDuration.compute(command_duration_samples[command_index]),
                    [AllocEvent(e.is_alloc, e.size, e.thread_index) for e in command_mem.allocs],
                )

            # Build a BenchmarkInvocation object that describes the results of this
            # invocation as they'll be persisted to the db
            invocations[benchmark.name] = BenchmarkInvocation(
                duration_num_samples,
                setup_duration,
                memory_profile.setup_net_bytes,
                teardown_duration,
                memory_profile.teardown_net_bytes,
                commands,
            )
        
        # Prepare a BenchmarkResults object that contains all of the profiling data
        # captured for this revision/platform/config, across all benchmarks
        res = BenchmarkResults(
            revision_name,
            platform,
            build_config,
            invocations
        )

    finally:
        server.stop()
        print('HTTP server stopped.')

    # Generate a SQL script that will insert all of the data we've captured into the db
    print('Recording benchmark results...')
    sql_text = store_benchmark_results(res)

    # Either write that SQL to a file or execute it against our local
    # benchmark-results.db file, depending on CLI args
    if args.output:
        with open(args.output, 'w') as fp:
            fp.write(sql_text)
        print(f'Wrote results to: {args.output}')
    else:
        with db_connect() as conn:
            cur = conn.cursor()
            cur.executescript(sql_text)
            conn.commit()
        print('Wrote results to local database file.')
