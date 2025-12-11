# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Data types used to store and retrieve benchmark results from the database.
"""
import math
import statistics
from dataclasses import dataclass
from typing import Tuple, List, Dict


def _stats_t_ppf(p: float, df: int) -> float:
    """
    Minimal drop-in replacement for scipy.stats.t.ppf(p, df) sufficient for confidence
    interval construction.

    Only handles probabilities in the upper tail (e.g., p=0.975 for 95% CI).
    For df > 30, falls back to the normal approximation.
    """
    # Critical values for t-distribution (two-tailed 95%) -> p = 0.975
    _t_critical_975 = {
        1: 12.706204736432095,
        2: 4.302652729696142,
        3: 3.182446305284263,
        4: 2.7764451051977987,
        5: 2.570581835636314,
        6: 2.4469118511449692,
        7: 2.3646242515927844,
        8: 2.306004135204166,
        9: 2.262157162854099,
        10: 2.2281388519649385,
        11: 2.200985160082949,
        12: 2.1788128296634177,
        13: 2.1603686564610127,
        14: 2.1447866879169273,
        15: 2.131449545559323,
        16: 2.1199052992210112,
        17: 2.1098155778331806,
        18: 2.10092204024096,
        19: 2.093024054408263,
        20: 2.085963447265837,
        21: 2.079613844727662,
        22: 2.0738730679040147,
        23: 2.0686576104190406,
        24: 2.063898561628021,
        25: 2.059538552753294,
        26: 2.055529438642871,
        27: 2.0518305164802833,
        28: 2.048407141795244,
        29: 2.0452296421327034,
        30: 2.0422724563012373,
    }

    # We only support p=0.975; enforce correctness.
    if abs(p - 0.975) > 1e-6:
        raise NotImplementedError(
            f"_stats_t_ppf currently only supports p = 0.975, got p = {p}"
        )

    if df <= 0:
        raise ValueError("degrees of freedom must be positive")

    # Lookup table for df=1..30
    if df in _t_critical_975:
        return _t_critical_975[df]

    # Normal approximation for large df
    return 1.96


@dataclass
class AggregateDuration:
    median: float
    mean: float
    stddev: float
    sem: float
    iqr: float
    ci95: Tuple[float, float]

    @classmethod
    def compute(cls, samples: List[float]):
        if not samples:
            raise ValueError("samples must not be empty")

        n = len(samples)
        s_sorted = sorted(samples)

        # Basic statistics
        mean = statistics.mean(samples)
        median = statistics.median(samples)
        stddev = statistics.stdev(samples)  # sample stddev (ddof=1)

        # Standard error of the mean
        sem = stddev / math.sqrt(n)

        # Quartiles for IQR
        q1 = statistics.quantiles(s_sorted, n=4)[0]  # 25%
        q3 = statistics.quantiles(s_sorted, n=4)[2]  # 75%
        iqr = q3 - q1

        # 95% confidence interval using t distribution
        df = n - 1
        tcrit = _stats_t_ppf(0.975, df)  # two-tailed 95%
        ci_low = mean - tcrit * sem
        ci_high = mean + tcrit * sem

        return cls(
            median=median,
            mean=mean,
            stddev=stddev,
            sem=sem,
            iqr=iqr,
            ci95=(ci_low, ci_high),
        )


@dataclass
class AllocEvent:
    is_alloc: bool
    size: int
    thread_index: int


@dataclass
class CommandExecution:
    label: str
    duration: AggregateDuration
    alloc_events: List[AllocEvent]


@dataclass
class BenchmarkInvocation:
    duration_num_samples: int
    setup_duration: AggregateDuration
    setup_net_bytes: int
    teardown_duration: AggregateDuration
    teardown_net_bytes: int
    commands: List[CommandExecution]


@dataclass
class BenchmarkResults:
    revision_name: str
    platform: str
    build_config: str
    benchmarks: Dict[str, BenchmarkInvocation]
