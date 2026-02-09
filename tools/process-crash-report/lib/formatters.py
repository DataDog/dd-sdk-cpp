# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Output formatting for crash reports and stack traces.

This module handles formatting and printing crash reports in different modes
(full or stack-only) and provides utilities for formatting individual frames
in both resolved and symbolicated forms.
"""

import sys
from enum import Enum
from typing import Optional, TextIO
from .models import CrashReport, StackFrame, SymbolizedFrame


class OutputMode(str, Enum):
    """
    Output modes for crash report display.

    FULL: Complete output including filename, metadata, resolved stack, and symbolicated stack
    STACK: Only the symbolicated (or resolved if no symbolication) stack trace, no headers
    """
    FULL = "full"
    STACK = "stack"


# === Frame Formatting ===

def format_resolved_frame(frame: StackFrame) -> str:
    """
    Format a resolved stack frame (module+offset, no symbolication).

    Formats:
    - Resolved: "0x00007fff12345678 (libfoo.dylib+0x1234)"
    - Unresolved: "0x00007fff12345678 (unresolved)"

    Args:
        frame: StackFrame to format

    Returns:
        Formatted string with address and module+offset
    """
    if frame.is_resolved and frame.module is not None and frame.offset is not None:
        return f"0x{frame.raw_address:016x} ({frame.module.name}+0x{frame.offset:x})"
    else:
        return f"0x{frame.raw_address:016x} (unresolved)"


def format_symbolicated_frame(sym_frame: SymbolizedFrame) -> str:
    """
    Format a symbolicated stack frame.

    Format: "  <frame_number>: <function> (<location>)"

    Example:
        "  5: malloc_zone_malloc (malloc.c:123)"
        "  0: main (main.cpp:42)"
        " 12: ?? (??:?)"

    Args:
        sym_frame: SymbolizedFrame to format

    Returns:
        Formatted string with frame number, function name, and location
    """
    return f"  {sym_frame.frame_number}: {sym_frame.function} ({sym_frame.location})"


# === Crash Report Printing ===

def print_crash_report(
    report: CrashReport,
    symbolized_stack: Optional[list[SymbolizedFrame]],
    mode: OutputMode,
    output: TextIO = sys.stdout
) -> None:
    """
    Print a crash report according to the specified output mode.

    FULL mode prints:
    1. Crash report filename
    2. Metadata section (key-value pairs)
    3. Resolved stack trace (address, module+offset)
    4. Symbolicated stack trace (or resolved if symbolized_stack is None)

    STACK mode prints:
    - Only the symbolicated stack (or resolved if symbolized_stack is None)
    - No headers, no metadata, just the stack frames

    Args:
        report: Parsed CrashReport object
        symbolized_stack: Optional list of symbolicated frames (if None, shows resolved only)
        mode: OutputMode enum (FULL or STACK)
        output: Output stream (defaults to stdout)
    """
    if mode == OutputMode.FULL:
        # === Section 1: Filename ===
        output.write("========================================================================\n")
        output.write(f"Crash Report: {report.file_path.name}\n")
        output.write("========================================================================\n")
        output.write("\n")

        # === Section 2: Metadata ===
        output.write("=== Crash Metadata ===\n")
        for key, value in report.metadata.items():
            output.write(f"{key}: {value}\n")
        output.write("\n")

        # === Section 3: Resolved Stack Trace ===
        output.write("=== Resolved Stack Trace ===\n")
        for frame in report.stack_frames:
            output.write(f"{format_resolved_frame(frame)}\n")
        output.write("\n")

        # === Section 4: Symbolicated Stack Trace ===
        if symbolized_stack is not None:
            output.write("=== Symbolicated Stack Trace ===\n")
            for sym_frame in symbolized_stack:
                output.write(f"{format_symbolicated_frame(sym_frame)}\n")
        else:
            output.write("=== Stack Trace (no symbolication) ===\n")
            for frame in report.stack_frames:
                output.write(f"  {frame.frame_number}: {format_resolved_frame(frame)}\n")

        output.write("\n")
        output.write("========================================================================\n")

    elif mode == OutputMode.STACK:
        # Stack-only mode: just the frames, no headers
        if symbolized_stack is not None:
            # Print symbolicated stack
            for sym_frame in symbolized_stack:
                output.write(f"{format_symbolicated_frame(sym_frame)}\n")
        else:
            # Print resolved stack (no symbolication)
            for frame in report.stack_frames:
                output.write(f"  {frame.frame_number}: {format_resolved_frame(frame)}\n")

    else:
        raise ValueError(f"Unknown output mode: {mode}")
