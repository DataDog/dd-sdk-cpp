# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Definitions for 'cpp.perf.core.*' metrics, which measure the performance of core SDK
operations.
"""
from lib.metric import BenchmarkInvocation, perf_metric


@perf_metric
def core__heap_hwm(sdk_start_and_stop: BenchmarkInvocation) -> int:
    """
    Max heap usage after starting and stopping the SDK with logging as the only feature. 
    """
    return sdk_start_and_stop.heap_hwm


@perf_metric
def core__create_time(sdk_start_and_stop: BenchmarkInvocation) -> float:
    """
    Time elapsed in Core::Create().
    """
    core_create = sdk_start_and_stop.find('Core::Create()')
    assert core_create
    return core_create.duration.median


@perf_metric
def core__start_time(sdk_start_and_stop: BenchmarkInvocation) -> float:
    """
    Time elapsed in Core::Start() with logging as the only feature.
    """
    core_start = sdk_start_and_stop.find('Core::Start()')
    assert core_start
    return core_start.duration.median


@perf_metric
def core__stop_time(sdk_start_and_stop: BenchmarkInvocation) -> float:
    """
    Time elapsed in Core::Stop() when no SDK functionality has been exercised.
    """
    core_stop = sdk_start_and_stop.find('Core::Stop()')
    assert core_stop
    return core_stop.duration.median
