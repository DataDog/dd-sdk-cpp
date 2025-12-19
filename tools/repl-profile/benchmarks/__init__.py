# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Collection of benchmarks used to profile the runtime performance of the C++ SDK. Each
benchmark is defined as a script containing a series of commands to be run in the repl
binary (see examples/repl) with profiling enabled.

To define a new benchmark, create a <benchmark_name>.py file in this directory, then
define a string called `INSTRUMENTED` that contains the set of repl commands to be run
with profiling enabled. You can optionally define `SETUP` and `TEARDOWN` to define
additional commands for which detailed profile data will not be collected.
"""
