# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for defining key performance metrics, evaluated based on the results of benchmark
invocations.
"""
import os
import inspect
import importlib
from typing import Callable, List, Protocol, Union

from db.models import BenchmarkInvocation

PerfMetricValue = Union[int, float]


class PerfMetricFunc(Protocol):
    """Protocol for functions decorated with @perf_metric."""
    is_perf_metric: bool            # Always set to True for decorated funcs
    metric_name: str                # Derived from function name, substituting '.' for '__'
    description: str                # Parsed from function docstring; may be empty
    benchmark_arg_names: List[str]  # Positional argument names; assumed to be benchmark names

    def __call__(self, *args, **kwargs) -> float: ...


def _resolve_metric_name(name: str) -> str:
    return 'cpp.perf.%s' % name.replace('__', '.')


def _resolve_metric_description(docstring: str) -> str:
    return docstring.replace('\n', ' ').replace('  ', '\n\n').strip()


def perf_metric(func: Callable[..., PerfMetricValue]) -> PerfMetricFunc:
    """
    Decorator to identify functions that evaluate performance metrics.

    Automatically inspects the function's parameters and stores their names
    for runtime resolution. Marks a function as a metric evaluator.
    """
    sig = inspect.signature(func)
    metric_func: PerfMetricFunc = func
    metric_func.is_perf_metric = True
    metric_func.metric_name = _resolve_metric_name(func.__name__)
    metric_func.description = _resolve_metric_description(inspect.getdoc(func) or '')
    metric_func.benchmark_arg_names = [name for name in sig.parameters.keys()]
    return metric_func


def collect_perf_metrics() -> List[PerfMetricFunc]:
    metric_funcs: List[PerfMetricFunc] = []
    metrics_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', 'metrics'))
    for filename in sorted(os.listdir(metrics_dir)):
        # Only consider Python source files
        filename_noext, ext = os.path.splitext(filename)
        if ext.lower() != '.py':
            continue

        # Import the Python file as a module
        module = importlib.import_module(f'metrics.{filename_noext}')
        for name in dir(module):
            if name.startswith('_'):
                continue
            value = getattr(module, name)
            if not inspect.isfunction(value):
                continue
            if not hasattr(value, 'is_perf_metric'):
                continue
            metric_func: PerfMetricFunc = value
            if metric_func.is_perf_metric is not True:
                continue
            metric_funcs.append(metric_func)

    # Return our accumulated list of PerfMetric functions
    return list(sorted(metric_funcs, key=lambda x: x.metric_name))
