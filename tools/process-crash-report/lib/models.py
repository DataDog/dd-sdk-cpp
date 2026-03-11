# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Data models for crash report processing.

This module defines the core data structures used throughout the crash report
processing pipeline: modules (loaded libraries), stack frames (raw and resolved),
symbolicated frames (with function/location info), and complete crash reports.
"""

from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass
class Module:
    """
    Represents a loaded module/library in a crash report.

    Attributes:
        base_address: Memory address where the module was loaded (start of range)
        end_address: End of the module's memory range (exclusive)
        path: Filesystem path to the module binary
        build_id: Build identifier for the module (platform-specific format):
                  - Windows: PE GUID+Age (uppercase hex, e.g., "F4A7B2C3D1E5...")
                  - macOS: Mach-O UUID (lowercase with dashes, e.g., "a1b2c3d4-...")
                  - Linux: ELF build-id (lowercase hex, e.g., "8c9d3e4f5a6b...")
                  - Empty string if extraction failed or not available
    """
    base_address: int
    end_address: int
    path: str
    build_id: str = ""

    @property
    def name(self) -> str:
        """Extract the module name from the path."""
        return Path(self.path).name


@dataclass
class StackFrame:
    """
    Represents a resolved stack frame.

    Contains the raw address from the crash report and optionally the resolved
    module and offset within that module. Does NOT contain symbolicated information
    (function names, line numbers) - that's in SymbolizedFrame.

    Attributes:
        raw_address: Original address from the crash report
        module: Resolved module this address belongs to (None if unresolved)
        offset: Offset within the module (None if unresolved)
        frame_number: Position in the stack trace (0-indexed)

    Note:
        The offset field represents the unmodified offset from the module's
        base_address at crash time. For DBH symbolication on Windows, base
        address adjustment happens during symbolication, not during resolution.
    """
    raw_address: int
    module: Optional[Module] = None
    offset: Optional[int] = None
    frame_number: int = 0

    @property
    def is_resolved(self) -> bool:
        """Check if this frame was successfully resolved to a module."""
        return self.module is not None and self.offset is not None


@dataclass
class SymbolizedFrame:
    """
    Result of symbolication for a single stack frame.

    Contains human-readable information about the function and source location
    for a stack frame, produced by running a symbolication tool (atos, llvm-symbolizer,
    addr2line, or dbh) on a resolved StackFrame.

    Attributes:
        frame_number: Frame index in the stack (0-indexed)
        function: Function name, or "??" if unknown/failed
        location: Source file and line (e.g., "file.cpp:42"), or "??:?" if unknown
        address_info: Optional additional address info (e.g., "module+0x1234")
    """
    frame_number: int
    function: str
    location: str
    address_info: Optional[str] = None


@dataclass
class RumContext:
    """
    RUM session identifiers recovered from the crash context file.

    Written alongside crash reports by the SDK whenever the RUM context
    changes. Each field is a UUID string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx)
    or None if that identifier was not active at the time of the crash (i.e.,
    the binary UUID was all-zeros).
    """
    application_id: Optional[str]
    session_id: Optional[str]
    view_id: Optional[str]
    action_id: Optional[str]


@dataclass
class CrashReport:
    """
    Parsed crash report data.

    Represents a complete crash report with metadata, resolved stack trace,
    and loaded modules. This is the primary data structure returned by the
    parser and used throughout the tool.

    Attributes:
        file_path: Path to the crash report file
        metadata: Key-value pairs from the metadata section (Signal, PID, TID, etc.)
        stack_frames: Resolved stack frames with module+offset information
        modules: List of loaded modules, sorted by base_address
        rum_context: RUM session identifiers from the accompanying .ctx file, or
                     None if the file was absent or could not be parsed
    """
    file_path: Path
    metadata: dict[str, str]
    stack_frames: list[StackFrame]
    modules: list[Module]
    rum_context: Optional[RumContext] = None
