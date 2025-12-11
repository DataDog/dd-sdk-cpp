# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for running queries to store or retrieve profiling data.
"""
import time
import sqlite3
from typing import Optional, Dict, List

from db.sql import SqlWriter
from db.models import (
    BenchmarkResults,
    BenchmarkInvocation,
    CommandExecution,
    AggregateDuration,
    AllocEvent
)


def store_benchmark_results(res: BenchmarkResults) -> str:
    sql = SqlWriter()
    recorded_at_ms = int(time.time() * 1000)

    # Wrap all commands in an explicit transaction
    sql.write('BEGIN')

    # Ensure that we have a revision record for the version of the SDK we used
    sql.write(
        'INSERT INTO revision (name, recorded_at) VALUES (?, ?) ON CONFLICT DO NOTHING',
        res.revision_name,
        recorded_at_ms,
    )

    # Delete any profiling data previously recorded for the same SDK revision, platform,
    # and build configuration
    sql.write(
        'DELETE FROM invocation WHERE revision_name = ? AND platform = ? AND build_config = ?',
        res.revision_name,
        res.platform,
        res.build_config,
    )

    # Insert invocation, command, and alloc_event rows for all benchmark invocations run
    # against this permutation of revision + platform + build config
    for benchmark_name, invocation in sorted(res.benchmarks.items()):
        # Record that we ran this benchmark in the configured environment
        sql.write('''
            INSERT INTO invocation (
                benchmark_name,
                revision_name,
                platform,
                build_config,
                recorded_at,
                duration_num_samples,
                setup_duration_median,
                setup_duration_mean,
                setup_duration_stddev,
                setup_duration_sem,
                setup_duration_iqr,
                setup_duration_ci95_lo,
                setup_duration_ci95_hi,
                setup_net_bytes,
                teardown_duration_median,
                teardown_duration_mean,
                teardown_duration_stddev,
                teardown_duration_sem,
                teardown_duration_iqr,
                teardown_duration_ci95_lo,
                teardown_duration_ci95_hi,
                teardown_net_bytes
            ) VALUES (
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?,
                ?
            )
            ''',
            benchmark_name,
            res.revision_name,
            res.platform,
            res.build_config,
            recorded_at_ms,
            invocation.duration_num_samples,
            invocation.setup_duration.median,
            invocation.setup_duration.mean,
            invocation.setup_duration.stddev,
            invocation.setup_duration.sem,
            invocation.setup_duration.iqr,
            invocation.setup_duration.ci95[0],
            invocation.setup_duration.ci95[1],
            invocation.setup_net_bytes,
            invocation.teardown_duration.median,
            invocation.teardown_duration.mean,
            invocation.teardown_duration.stddev,
            invocation.teardown_duration.sem,
            invocation.teardown_duration.iqr,
            invocation.teardown_duration.ci95[0],
            invocation.teardown_duration.ci95[1],
            invocation.teardown_net_bytes
        )

        # Record each command that was executed in that benchmark invocation
        for command in invocation.commands:
            sql.write('''
                INSERT INTO command (
                    invocation_id,
                    label,
                    duration_median,
                    duration_mean,
                    duration_stddev,
                    duration_sem,
                    duration_iqr,
                    duration_ci95_lo,
                    duration_ci95_hi
                ) VALUES (
                    (SELECT id FROM invocation ORDER BY id DESC LIMIT 1),
                    ?,
                    ?,
                    ?,
                    ?,
                    ?,
                    ?,
                    ?,
                    ?
                )
                ''',
                command.label,
                command.duration.median,
                command.duration.mean,
                command.duration.stddev,
                command.duration.sem,
                command.duration.iqr,
                command.duration.ci95[0],
                command.duration.ci95[1]
            )

            # Record every individual alloc/free captured for that command execution
            for event in command.alloc_events:
                sql.write('''
                    INSERT INTO alloc_event (
                        command_id,
                        is_alloc,
                        size,
                        thread_index
                    ) VALUES (
                        (SELECT id FROM command ORDER BY id DESC LIMIT 1),
                        ?,
                        ?,
                        ?
                    )
                    ''',
                    int(event.is_alloc),
                    event.size,
                    event.thread_index
                )

    # Ensure that our transaction is committed, and return our final SQL script
    sql.write('COMMIT')
    return sql.data


def retrieve_benchmark_results(conn: sqlite3.Connection, revision_name: str, platform: str, build_config: str) -> Optional[BenchmarkResults]:
    # Query for all invocations matching the given criteria
    invocation_rows = conn.execute('''
        SELECT
            id,
            benchmark_name,
            duration_num_samples,
            setup_duration_median,
            setup_duration_mean,
            setup_duration_stddev,
            setup_duration_sem,
            setup_duration_iqr,
            setup_duration_ci95_lo,
            setup_duration_ci95_hi,
            setup_net_bytes,
            teardown_duration_median,
            teardown_duration_mean,
            teardown_duration_stddev,
            teardown_duration_sem,
            teardown_duration_iqr,
            teardown_duration_ci95_lo,
            teardown_duration_ci95_hi,
            teardown_net_bytes
        FROM invocation
        WHERE revision_name = ? AND platform = ? AND build_config = ?
    ''', (revision_name, platform, build_config)).fetchall()

    # If no invocations found, return None
    if not invocation_rows:
        return None

    # Build the benchmarks dictionary
    benchmarks: Dict[str, BenchmarkInvocation] = {}

    for invocation_row in invocation_rows:
        # Query for all commands associated with this invocation
        command_rows = conn.execute('''
            SELECT
                id,
                label,
                duration_median,
                duration_mean,
                duration_stddev,
                duration_sem,
                duration_iqr,
                duration_ci95_lo,
                duration_ci95_hi
            FROM command
            WHERE invocation_id = ?
        ''', (invocation_row['id'],)).fetchall()

        # Reconstruct CommandExecution objects
        commands: List[CommandExecution] = []
        for command_row in command_rows:
            # Query for all alloc_events associated with this command
            alloc_event_rows = conn.execute('''
                SELECT is_alloc, size, thread_index
                FROM alloc_event
                WHERE command_id = ?
            ''', (command_row['id'],)).fetchall()

            commands.append(CommandExecution(
                label=command_row['label'],
                duration=AggregateDuration(
                    median=command_row['duration_median'],
                    mean=command_row['duration_mean'],
                    stddev=command_row['duration_stddev'],
                    sem=command_row['duration_sem'],
                    iqr=command_row['duration_iqr'],
                    ci95=(command_row['duration_ci95_lo'], command_row['duration_ci95_hi'])
                ),
                alloc_events=[
                    AllocEvent(
                        is_alloc=bool(event['is_alloc']),
                        size=event['size'],
                        thread_index=event['thread_index']
                    )
                    for event in alloc_event_rows
                ],
            ))

        # Create the BenchmarkInvocation object
        benchmarks[invocation_row['benchmark_name']] = BenchmarkInvocation(
            duration_num_samples=invocation_row['duration_num_samples'],
            setup_duration=AggregateDuration(
                median=invocation_row['setup_duration_median'],
                mean=invocation_row['setup_duration_mean'],
                stddev=invocation_row['setup_duration_stddev'],
                sem=invocation_row['setup_duration_sem'],
                iqr=invocation_row['setup_duration_iqr'],
                ci95=(invocation_row['setup_duration_ci95_lo'], invocation_row['setup_duration_ci95_hi'])
            ),
            setup_net_bytes=invocation_row['setup_net_bytes'],
            teardown_duration=AggregateDuration(
                median=invocation_row['teardown_duration_median'],
                mean=invocation_row['teardown_duration_mean'],
                stddev=invocation_row['teardown_duration_stddev'],
                sem=invocation_row['teardown_duration_sem'],
                iqr=invocation_row['teardown_duration_iqr'],
                ci95=(invocation_row['teardown_duration_ci95_lo'], invocation_row['teardown_duration_ci95_hi'])
            ),
            teardown_net_bytes=invocation_row['teardown_net_bytes'],
            commands=commands
        )

    return BenchmarkResults(
        revision_name=revision_name,
        platform=platform,
        build_config=build_config,
        benchmarks=benchmarks
    )
