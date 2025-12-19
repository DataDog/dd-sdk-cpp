# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Code for parsing profiling data from repl output.

The repl includes basic profiling capabilities for tracking the CPU and memory overhead
of individual SDK operations. After running 'start-profile', every command will be
accompanied by output like this:

> start-profile
<<<(main-thread-id): 3241635350488800441
< Profiling started.
<<(duration): 60333
<<(alloc-events): 0
> create-logger
< Logging::CreateLogger()
<<(duration): 183125
<<(alloc-events): 5
<<<(0): op:alloc size:2792 tid:3241635350488800441
<<<(1): op:alloc size:32 tid:3241635350488800441
<<<(2): op:alloc size:264 tid:3241635350488800441
<<<(3): op:alloc size:32 tid:3241635350488800441
<<<(4): op:alloc size:32 tid:3241635350488800441

The code in this file is used to parse this output in order to assemble metrics about
SDK performance.
"""
import re
from dataclasses import dataclass, field
from typing import List


# <<<(main-thread-id): 3241635350488800441
__main_thread_id_regex__ = re.compile(r'^<<<\(main-thread-id\): (\d+)')

# < Logging::CreateLogger()
__command_result_regex__ = re.compile(r'^<+ (.*)')

# <<(duration): 183125
__duration_regex__ = re.compile(r'^<<\(duration\): (\d+)')

# <<(alloc-events): 5
__num_allocs_regex__ = re.compile(r'^<<\(alloc-events\): (\d+)')

# <<<(0): op:alloc size:2792 tid:3241635350488800441
__alloc_event_regex__ = re.compile(r'^<<<\((\d+)\): op:(alloc|free) size:(\d+) tid:(\d+)')


def _strip_ansi(s: str) -> str:
    return re.compile(r'\x1B\[[0-?]*[ -/]*[@-~]').sub('', s)


@dataclass
class AllocEvent:
    is_alloc: bool
    size: int
    thread_index: int


@dataclass
class ProfiledCommand:
    label: str
    duration_ns: int = -1
    num_allocs: int = -1
    allocs: List[AllocEvent] = field(default_factory=list)


@dataclass
class Profile:
    mode: str = 'setup'
    thread_ids: List[int] = field(default_factory=list)

    setup_duration_ns: int = 0
    setup_net_bytes: int = 0
    teardown_duration_ns: int = 0
    teardown_net_bytes: int = 0

    commands: List[ProfiledCommand] = field(default_factory=list)

    def _register_main_thread_id(self, tid: int):
        assert not self.thread_ids
        self.thread_ids.append(tid)

    def _resolve_thread_index(self, tid: int):
        try:
            return self.thread_ids.index(tid)
        except ValueError:
            thread_index = len(self.thread_ids)
            self.thread_ids.append(tid)
            return thread_index

    def _handle_command(self, label: str):
        if self.mode == 'setup' and label == 'BEGIN PROFILING':
            self.mode = 'instrumented'
        elif self.mode == 'instrumented':
            if label == 'END PROFILING':
                self.mode = 'teardown'
            else:
                self.commands.append(ProfiledCommand(label))

    def _handle_duration(self, duration_ns: int):
        if self.mode == 'setup':
            self.setup_duration_ns += duration_ns
        elif self.mode == 'instrumented':
            if self.commands:
                assert self.commands[-1].duration_ns == -1
                self.commands[-1].duration_ns = duration_ns
        elif self.mode == 'teardown':
            self.teardown_duration_ns += duration_ns

    def _handle_num_allocs(self, num_allocs: int):
        if self.mode == 'instrumented':
            if self.commands:
                assert self.commands[-1].num_allocs == -1
                self.commands[-1].num_allocs = num_allocs

    def _handle_alloc(self, i: int, op: str, size: int, tid: int):
        assert op in ('alloc', 'free')
        is_alloc = op == 'alloc'
        thread_index = self._resolve_thread_index(tid)
        if self.mode == 'setup':
            delta_bytes = size if is_alloc else -size
            self.setup_net_bytes += delta_bytes
        elif self.mode == 'instrumented':
            assert self.commands
            assert len(self.commands[-1].allocs) == i
            self.commands[-1].allocs.append(AllocEvent(is_alloc, size, thread_index))
        elif self.mode == 'teardown':
            delta_bytes = size if is_alloc else -size
            self.teardown_net_bytes += delta_bytes

    def read(self, s: str):
        line = _strip_ansi(s)

        if not self.thread_ids:
            main_thread_id_match = __main_thread_id_regex__.match(line)
            if main_thread_id_match:
                tid = int(main_thread_id_match.group(1))
                self._register_main_thread_id(tid)
                return

        command_result_match = __command_result_regex__.match(line)
        if command_result_match:
            label = command_result_match.group(1)
            self._handle_command(label)
            return

        duration_match = __duration_regex__.match(line)
        if duration_match:
            duration_ns = int(duration_match.group(1))
            self._handle_duration(duration_ns)
            return

        num_allocs_match = __num_allocs_regex__.match(line)
        if num_allocs_match:
            num_allocs = int(num_allocs_match.group(1))
            self._handle_num_allocs(num_allocs)
            return

        alloc_event_match = __alloc_event_regex__.match(line)
        if alloc_event_match:
            i = int(alloc_event_match.group(1))
            op = alloc_event_match.group(2)
            size = int(alloc_event_match.group(3))
            tid = int(alloc_event_match.group(4))
            self._handle_alloc(i, op, size, tid)
