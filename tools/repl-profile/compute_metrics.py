# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
repl-profile/compute_metrics
Usage: python3 tools/repl-profile/compute_metrics.py
Requires no external dependencies unless using --upload.

If using --upload, Datadog API credentials must be set in both DD_API_KEY and DD_APP_KEY
env vars, and datadog-api-client (included in requirements.txt) must be installed.
"""
import re
import sys
import json
import argparse
from typing import List, Tuple, Optional, Dict, Any

from db.connection import db_connect
from db.models import BenchmarkResults, BenchmarkInvocation
from db.queries import retrieve_benchmark_results

from lib.config import auto_resolve_platform, auto_resolve_build_config
from lib.metric import collect_perf_metrics, PerfMetricValue, PerfMetricFunc
from lib.dd import upload_metrics, update_metrics_metadata


def resolve_revisions(target: str, baseline: Optional[str]) -> Tuple[str, Optional[str]]:
    if target != 'latest' and baseline != 'previous':
        return target, baseline

    with db_connect() as conn:
        if target == 'latest':
            row = conn.execute('''
                SELECT revision_name FROM invocation
                WHERE platform = ? AND build_config = ?
                ORDER BY recorded_at DESC
                LIMIT 1
            ''', (platform, build_config)).fetchone()
            if not row:
                print(f'ERROR: No benchmark data is recorded for {platform} {build_config}')
                sys.exit(1)
            target = row['revision_name']
        
        if baseline == 'previous':
            row = conn.execute('''
                SELECT name FROM revision
                WHERE recorded_at < (SELECT recorded_at FROM revision WHERE name = ?)
                ORDER BY recorded_at DESC 
                LIMIT 1
            ''', (target,)).fetchone()
            if not row:
                print(f'ERROR: {target} has no previous revision')
                sys.exit(1)
            baseline = row['name']
        
    return target, baseline


def compute_metric(res: BenchmarkResults, metric_func: PerfMetricFunc) -> Tuple[Optional[PerfMetricValue], List[str]]:
    # Resolve BenchmarkInvocation stats for each named parameter of the metric func
    arg_values: List[BenchmarkInvocation] = []
    missing_benchmark_names: List[str] = []
    for benchmark_name in metric_func.benchmark_arg_names:
        invocation = res.benchmarks.get(benchmark_name)
        if invocation:
            arg_values.append(invocation)
        else:
            missing_benchmark_names.append(benchmark_name)
    
    # Report no value for this metric if no data exists for any of the benchmarks it
    # depends on
    if missing_benchmark_names:
        return None, missing_benchmark_names

    # Pass the set of benchmark results to the function to compute its value for the
    # chosen revision, platform, and build config
    metric_value = metric_func(*arg_values)
    return metric_value, []


def pretty_metric(value: Optional[PerfMetricValue], baseline: Optional[PerfMetricValue] = None) -> str:
    if value is None:
        return 'n/a'
    
    prefix = ''
    if baseline is not None:
        delta = value - baseline
        if delta >= 0.0:
            prefix = '\x1b[0;31m▴\x1b[0m'
        else:
            prefix = '\x1b[0;32m▾\x1b[0m'
    
    def to_pretty_value():
        if isinstance(value, int):
            return str(value)
        if isinstance(value, float):
            # Apply SI prefixes for appropriate orders of magnitude
            if value < 1e-6:  # nano (< 1 microsecond)
                return f'{value * 1e9:.2f}n'
            elif value < 1e-3:  # micro (< 1 millisecond)
                return f'{value * 1e6:.2f}µ'
            elif value < 1.0:  # milli (< 1 second)
                return f'{value * 1e3:.2f}m'
            else:
                return f'{value:.2f}'
        return repr(value)

    return prefix + to_pretty_value()


def pretty_description(s: str) -> str:
    if not s:
        return ''
    maxlen = 60
    summary = s.splitlines()[0]
    if len(summary) > maxlen + 3:
        return summary[:maxlen] + '...'
    return summary


def tabulate(rows: List[List[str]], sep: str = ' | '):
    ansi_len = lambda s: len(re.sub(r'\x1b\[[0-9;]*m', '', s))
    widths = [max(ansi_len(str(row[i])) for row in rows) for i in range(len(rows[0]))]
    total_width = (sum(widths) + (len(sep) * (len(rows[0]) - 1))) if rows else 0
    print('=' * total_width)
    for i, row in enumerate(rows):
        formatted_cells = []
        for col_idx, cell in enumerate(row):
            cell_str = str(cell)
            padding = widths[col_idx] - ansi_len(cell_str)
            formatted_cells.append(cell_str + ' ' * padding)
        print(sep.join(formatted_cells))
        if i == 0:
            print('-' * total_width)
    print('=' * total_width)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--revision', default='latest', help='Name of the SDK revision for which metrics should be computed. Omit to use the latest revision in the db.')
    parser.add_argument('--platform', default='auto', help='Name of the platform for which metrics should be computed. Omit to resolve from sys.platform.')
    parser.add_argument('--config', default='auto', help='Name of the build configuration for which profiling data is recorded. Omit to infer from CMakeCache.txt if possible.')
    parser.add_argument('--json', action='store_true', help='If true, print gathered metrics as a JSON object rather than a pretty table.')
    parser.add_argument('--compare-to', help='Revision to use as a baseline: if set, displays computed metrics alongside metrics from this revision.')
    parser.add_argument('--upload', action='store_true', help='If true, upload metrics to Datadog in the mobile-integration org, tagged with revision details. Requires DD_API_KEY and DD_APP_KEY.')
    parser.add_argument('--canonical', action='store_true', help='If true, these metrics will be tagged as canonical for the given platform, build config, and revision.')
    args = parser.parse_args()

    # Import all the perf metric functions from metrics/
    metrics = collect_perf_metrics()
    if not metrics:
        print('ERROR: No @perf_metric functions found in metrics/')
        sys.exit(1)

    # Resolve platform and build config if necessary
    platform = auto_resolve_platform() if args.platform == 'auto' else args.platform
    build_config = auto_resolve_build_config() if args.config == 'auto' else args.config

    # If we weren't given an explicit revision name, check the db for the most recent
    # benchmark invocation that matches our platform and build config; and if we were
    # given '--compare-to previous', automatically resolve the revision prior
    revision_name, baseline_revision_name = resolve_revisions(args.revision, args.compare_to)

    # If we're comparing to a baseline revision, make sure it's actually distinct from
    # the revision we're computing metrics for
    if baseline_revision_name == revision_name:
        print(f'ERROR: Baseline revision must be different from target revision ({revision_name}); specify a different --revision or --compare-to')
        sys.exit(1)

    # Pull the full set of benchmark data from the benchmark-results.db for the selected
    # revision, platform, and build config: this reconsitutes the same data structure
    # we'd get from running the benchmark directly
    baseline_res: Optional[BenchmarkResults] = None
    with db_connect() as conn:
        res = retrieve_benchmark_results(conn, revision_name, platform, build_config)
        if not res:
            print(f'ERROR: No results recorded for {revision_name} ({platform} {build_config})')
            sys.exit(1)

        if baseline_revision_name:
            baseline_res = retrieve_benchmark_results(conn, baseline_revision_name, platform, build_config)
            if not baseline_res:
                print(f'ERROR: No results recorded for baseline {revision_name} ({platform} {build_config})')
                sys.exit(1)

    # Iterate over every metric function, attempting to resolve the results for the
    # benchmark(s) names as function parameters, then evaluating the function to get its
    # value as either an int or a float
    old_values: List[Optional[PerfMetricValue]] = []
    new_values: List[Optional[PerfMetricValue]] = []
    for metric_func in metrics:
        # If we're comparing against a baseline, recompute the metric from that
        # revision's stored benchmark results
        old_value: Optional[PerfMetricValue] = None
        if baseline_res:
            old_value, _ = compute_metric(baseline_res, metric_func)
            old_values.append(old_value)

        # Compute the metric value from our target revision's benchmark results,
        # collecting all values in a list
        new_value, missing_benchmark_names = compute_metric(res, metric_func)
        new_values.append(new_value)
        if new_value is None or missing_benchmark_names:
            print(f'WARNING: Unable to evaluate metric {metric_func.metric_name}: no data available for benchmark(s) {", ".join(missing_benchmark_names)}')
    
    # Now that we have all required metric values, print them in the desired format, and
    # collect the subset of new_values with valid results so we can upload them
    if args.json:
        metrics_data: List[Dict[str, Any]] = []
        for i, metric_func in enumerate(metrics):
            metrics_data.append({
                'name': metric_func.metric_name,
                'value': new_values[i],
            })
            if old_values:
                metrics_data[-1]['baseline'] = old_values[i]
        data = {
            'revision': revision_name,
            'platform': platform,
            'build_config': build_config,
            'metrics': metrics_data,
        }
        if baseline_revision_name:
            data ['baseline_revision'] = baseline_revision_name
        json.dump(data, sys.stdout)
    elif old_values:
        rows = [['Metric', baseline_revision_name, revision_name, 'Description']]
        for i, metric_func in enumerate(metrics):
            old_value = old_values[i]
            new_value = new_values[i]
            delta = (new_value - old_value) if (new_value and old_value) else None
            rows.append([
                metric_func.metric_name,
                pretty_metric(old_value),
                pretty_metric(new_value, baseline=old_value),
                pretty_description(metric_func.description),
            ])
        tabulate(rows)
    else:
        rows = [['Metric', revision_name, 'Description']]
        for i, metric_func in enumerate(metrics):
            new_value = new_values[i]
            rows.append([
                metric_func.metric_name,
                pretty_metric(new_value),
                pretty_description(metric_func.description),
            ])
        tabulate(rows)

    # Upload our new metrics via the Datadog API if configured to do so
    if args.upload:
        # Gather (name, value) pairs for all metrics that we were able to compute values
        # for in the target revision
        metric_values: List[Tuple[str, PerfMetricValue]] = []
        for metric_func, new_value in zip(metrics, new_values):
            if new_value:
                metric_values.append((metric_func.metric_name, new_value))
        if not metric_values:
            print('ERROR: Unable to upload metrics: computed no values')
            sys.exit(1)

        # Apply the appropriate tags to identify the target revision and build/runtime
        # environment. 'veracity:canon' ensures that we can build filters/monitors that
        # only consider data uploaded from controlled CI builds with --canonical.
        tags = [
            f'version:{revision_name}',
            f'platform:{platform}',
            f'build:{build_config}',
            f'veracity:{"canon" if args.canonical else "test"}'
        ]

        # Use the Datadog v2 metrics API's submit_metrics endpoint to upload these new
        # data points
        upload_metrics(metric_values, tags)

        # Use the Datadog v1 metrics API's update_metric_metadata endpoint to update
        # descriptions etc. based on what's defined in metrics/
        update_metrics_metadata(metrics)
