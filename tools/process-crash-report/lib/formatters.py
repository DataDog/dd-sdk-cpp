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
import uuid
from enum import Enum
from pathlib import Path
from typing import Optional, TextIO
from .models import CrashReport, StackFrame, SymbolizedFrame, Module, RumContext


class OutputMode(str, Enum):
    """
    Output modes for crash report display.

    FULL: Complete output including filename, metadata, resolved stack, and symbolicated stack
    STACK: Only the symbolicated (or resolved if no symbolication) stack trace, no headers
    RAW: Raw parsed binary crash report without resolution or symbolication
    JSON: RUM Error Event as a JSON object
    """
    FULL = "full"
    STACK = "stack"
    RAW = "raw"
    JSON = "json"


# === Frame Formatting ===

def format_resolved_frame(frame: StackFrame) -> str:
    """
    Format a resolved stack frame in Apple crash report format.

    The RUM symbolication backend expects four whitespace-separated fields:

      <n>   <binary>    0x<abs_addr> 0x<load_addr> + <decimal_offset>

    The parser extracts the absolute instruction address and load address
    separately, computing offset = instr_addr - load_addr to look up the
    symbol. This format is required for all platforms (mach-o, ELF, PE).
    """
    n = frame.frame_number
    if frame.is_resolved and frame.module is not None and frame.offset is not None:
        abs_addr = f"0x{frame.raw_address:016x}"
        load_addr = f"0x{frame.module.base_address:016x}"
        return f"{n}   {frame.module.name}\t{abs_addr} {load_addr} + {frame.offset}"
    else:
        abs_addr = f"0x{frame.raw_address:016x}"
        return f"{n}   ???\t{abs_addr} 0x0000000000000000 + 0"


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


# === RUM Context Formatting ===

def _print_rum_context(rum_context: RumContext, output: TextIO) -> None:
    """Print non-None RUM context fields as key-value lines."""
    if rum_context.application_id is not None:
        output.write(f"Application ID: {rum_context.application_id}\n")
    if rum_context.session_id is not None:
        output.write(f"Session ID: {rum_context.session_id}\n")
    if rum_context.view_id is not None:
        output.write(f"View ID: {rum_context.view_id}\n")
    if rum_context.action_id is not None:
        output.write(f"Action ID: {rum_context.action_id}\n")


# === Raw Crash Report Printing ===

def print_raw_crash_report(
    file_path: Path,
    metadata: dict[str, str],
    stack_addresses: list[int],
    modules: list[Module],
    rum_context: Optional[RumContext] = None,
    output: TextIO = sys.stdout
) -> None:
    """
    Print a raw crash report showing parsed binary data without resolution or symbolication.

    This mode displays the crash metadata, raw stack addresses, and loaded modules
    exactly as parsed from the binary file, without performing address resolution
    or symbolication.

    Args:
        file_path: Path to the crash report file
        metadata: Parsed metadata dictionary from crash report header
        stack_addresses: Raw addresses from stack frame section
        modules: Parsed Module objects from module section
        rum_context: Optional RUM session context from the accompanying .ctx file
        output: Output stream (defaults to stdout)
    """
    # === Section 1: Filename ===
    output.write("========================================================================\n")
    output.write(f"Crash Report: {file_path.name}\n")
    output.write("========================================================================\n")
    output.write("\n")

    # === Section 2: Metadata ===
    output.write("=== Crash Metadata ===\n")
    for key, value in metadata.items():
        output.write(f"{key}: {value}\n")
    if rum_context is not None:
        _print_rum_context(rum_context, output)
    output.write("\n")

    # === Section 3: Raw Stack Trace ===
    output.write("=== Stack Trace (Raw Addresses) ===\n")
    for address in stack_addresses:
        output.write(f"0x{address:016x}\n")
    output.write("\n")

    # === Section 4: Loaded Modules ===
    output.write("=== Loaded Modules ===\n")
    for module in modules:
        build_id_str = f" [{module.build_id}]" if module.build_id else ""
        output.write(f"0x{module.base_address:016x}-0x{module.end_address:016x} {module.path}{build_id_str}\n")
    output.write("\n")

    output.write("========================================================================\n")


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
        if report.rum_context is not None:
            _print_rum_context(report.rum_context, output)
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


# === RUM Error Event Formatting ===

_SYSTEM_PATH_PREFIXES = (
    "/System/",
    "/usr/lib/",
    "/usr/local/lib/",
    "/Library/Apple/",
    "/lib/",
    "/lib64/",
    "/usr/lib64/",
    "C:\\Windows\\System32\\",
    "C:\\Windows\\SysWOW64\\",
)


def _is_system_module(path: str) -> bool:
    return any(path.startswith(p) for p in _SYSTEM_PATH_PREFIXES)


def _infer_platform(modules: list[Module]) -> str:
    # Windows modules carry .exe or .dll in their paths; check these first
    # since Windows paths may also contain .so in unusual third-party paths.
    if any(".exe" in m.path or ".dll" in m.path for m in modules):
        return "win32"
    if any(".dylib" in m.path for m in modules):
        return "darwin"
    if any(".so" in m.path for m in modules):
        return "linux"
    # No module-path evidence — fall back to the host platform.
    return sys.platform


def _format_binary_image(module: Module) -> dict:
    return {
        "uuid": module.build_id,
        "name": module.name,
        "is_system": _is_system_module(module.path),
        "load_address": hex(module.base_address),
        "max_address": hex(module.end_address),
    }




def format_rum_error_event(
    report: CrashReport,
    symbolized_stack: Optional[list[SymbolizedFrame]],
) -> dict:
    """
    Build a RUM Error Event dict from a crash report.

    Requires that `report.rum_context` contains a non-None `application_id`;
    raises `ValueError` otherwise. The returned dict conforms to the RUM Error
    Event schema and is ready to be serialized with `json.dumps`.

    Optional context fields (`session`, `view`, `action`) are included only
    when the corresponding IDs are present in `report.rum_context`.

    The `error.stack` uses symbolicated frames when `symbolized_stack` is
    provided, falling back to resolved (module+offset) frames otherwise.
    """
    rum_context = report.rum_context
    if rum_context is None or rum_context.application_id is None:
        raise ValueError(
            "No RUM application ID in crash context; "
            "cannot produce a RUM Error Event"
        )

    # Extract the raw seconds-since-epoch from the formatted timestamp string
    # (format: "1770922852 (2026-02-12 18:14:12 UTC)")
    timestamp_str = report.metadata.get("Timestamp", "0")
    timestamp_seconds = int(timestamp_str.split()[0])
    date_ms = timestamp_seconds * 1000

    # Derive a short signal/exception name for the error message
    if "Signal" in report.metadata:
        # e.g. "SIGSEGV (11)" → "SIGSEGV"
        signal_name = report.metadata["Signal"].split()[0]
    elif "Exception Code" in report.metadata:
        signal_name = report.metadata["Exception Code"]
    else:
        signal_name = "Unknown"

    fault_addr = report.metadata.get("Fault Address", "unknown")
    error_message = f"{signal_name} at {fault_addr}"

    _PLATFORM_SOURCE_TYPE = {
        "win32": "pe",
        "darwin": "ios",
        "linux": "elf",
    }
    platform = _infer_platform(report.modules)
    source_type = _PLATFORM_SOURCE_TYPE.get(platform, "elf")

    # Build the newline-delimited stack string.
    if symbolized_stack is not None:
        stack_lines = [f"{f.function} ({f.location})" for f in symbolized_stack]
    else:
        stack_lines = [format_resolved_frame(f) for f in report.stack_frames]
    stack_str = "\n".join(stack_lines) + "\n"

    event: dict = {
        "type": "error",
        "application": {"id": rum_context.application_id},
        "date": date_ms,
        "_dd": {"format_version": 2},
        "error": {
            "id": str(uuid.uuid4()),
            "message": error_message,
            "source": "source",
            "source_type": source_type,
            "stack": stack_str,
            "is_crash": True,
        },
    }

    if rum_context.session_id is not None:
        event["session"] = {"id": rum_context.session_id, "type": "user"}
    if rum_context.view_id is not None:
        event["view"] = {"id": rum_context.view_id, "url": "placeholder"}
    if rum_context.action_id is not None:
        event["action"] = {"id": rum_context.action_id}

    event["error"]["binary_images"] = [_format_binary_image(m) for m in report.modules]

    return event
