# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Definitions for 'cpp.perf.logging.*' metrics, which measure the performance of key
operations in the logging API.
"""
import statistics

from lib.metric import BenchmarkInvocation, perf_metric


@perf_metric
def logging__heap_hwm(log_50: BenchmarkInvocation) -> int:
    """
    Maximum heap usage of the SDK when logging is the only feature registered.
    """
    return log_50.heap_hwm


@perf_metric
def logger__log_time__cold(log_50: BenchmarkInvocation) -> float:
    """
    Duration of first Logger::Log() call made from a cold SDK.
    """
    first_log_call = log_50.commands[0]
    assert first_log_call.label == 'Logger::Log()'
    return first_log_call.duration.median


@perf_metric
def logger__log_time__warm(log_50: BenchmarkInvocation) -> float:
    """
    Median duration of a call to Logger::Log() with a warm SDK.
    """
    log_calls = log_50.find_all('Logger::Log()')
    log_calls = log_calls[5:]
    return statistics.median([x.duration.mean for x in log_calls])
