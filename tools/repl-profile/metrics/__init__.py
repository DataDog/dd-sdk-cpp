# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Definitions for C++ SDK performance metrics. Each performance metric is a function of
the results of one or more benchmark invocations, resolving to a single numeric value
that provides some information about the performance of the SDK.

To create a new metric, define a function in this module decorated with @perf_metric,
and list the names of all benchmarks that your metric depends on as function parameters.
The body of the function should evaluate the value based on the provided stats recorded
from that benchmark. For example:

    @perf_metric
    def foo__some_function_time(some_benchmark: BenchmarkInvocation) -> float:
        '''Median Time taken to execute Some::Function()'''
        some_function = some_benchmark.find('Some::Function()')
        assert some_function
        return some_function.elapsed.median

Adding this function and running:

- python tools/repl-profile/run_benchmarks.py --revision 0.1.0 --upload

...would result in a new metric called 'cpp.perf.foo.some_function_time' being uploaded
to Datadog, with the description provided in the function docstring automatically added
as metadata.
"""
