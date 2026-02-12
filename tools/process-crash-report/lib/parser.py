# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Parse crash report files into structured data.

This module handles reading crash report files from disk, parsing their three
main sections (metadata, stack trace, loaded modules), and combining parsed
data with address resolution to produce complete CrashReport objects.
"""

from pathlib import Path
from typing import Optional
from .models import CrashReport, Module


# === Hex Address Parsing ===

def _parse_hex_address(addr_str: str) -> int:
    """
    Parse a hex address string to an integer.

    Args:
        addr_str: Hex string like "0x12345678"

    Returns:
        Integer address

    Raises:
        ValueError: If string is not valid hex
    """
    return int(addr_str, 16)


# === Crash Report Discovery ===

def find_latest_crash_report(crashes_dir: Path) -> Optional[Path]:
    """
    Find the most recent crash report in the crashes directory.

    Crash reports are named crash_<timestamp>_<pid>.txt, where timestamp
    uses lexicographically sortable format. This function finds the latest
    by sorting in reverse order.

    Args:
        crashes_dir: Directory to search (typically .crashes/)

    Returns:
        Path to most recent crash_*.txt file, or None if none found or
        directory doesn't exist
    """
    if not crashes_dir.exists() or not crashes_dir.is_dir():
        return None

    crash_files = sorted(
        crashes_dir.glob('crash_*.txt'),
        reverse=True  # Most recent first (lexical sort)
    )

    return crash_files[0] if crash_files else None


# === Crash Report Parsing ===

def parse_crash_report(file_path: Path) -> tuple[dict[str, str], list[int], list[Module]]:
    """
    Parse a crash report file into components.

    Crash reports have three sections:
    1. Metadata: Key-value pairs (e.g., "Signal: SIGSEGV")
    2. Stack trace (raw addresses): Lines starting with "0x"
    3. Loaded modules: Lines with format "0xbase-0xend /path/to/module"

    Section markers:
    - "Stack trace" (case-insensitive) starts stack trace section
    - "Loaded Modules" (case-insensitive) starts module section
    - "===" ends sections or marks report boundaries

    Args:
        file_path: Path to crash report file

    Returns:
        Tuple of (metadata_dict, stack_addresses, modules)
        - metadata_dict: Key-value pairs from header section
        - stack_addresses: Raw hex addresses from stack trace section (in order)
        - modules: Parsed Module objects from loaded modules section

    Raises:
        FileNotFoundError: If file doesn't exist
        ValueError: If file format is invalid or cannot be parsed
    """
    metadata: dict[str, str] = {}
    stack_addresses: list[int] = []
    modules: list[Module] = []

    # State machine for section tracking
    in_stack_trace = False
    in_loaded_modules = False

    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()

                # Skip empty lines
                if not line:
                    continue

                # === Metadata Section ===
                # Before any section markers, lines with ": " are metadata
                if not in_stack_trace and not in_loaded_modules and ': ' in line:
                    key, value = line.split(': ', 1)
                    metadata[key] = value

                # === Section Markers ===
                if line.lower().startswith('stack trace'):
                    in_stack_trace = True
                    in_loaded_modules = False
                    continue
                elif line.lower().startswith('loaded modules'):
                    in_stack_trace = False
                    in_loaded_modules = True
                    continue
                elif line.startswith('==='):
                    # End of report or section separator
                    in_stack_trace = False
                    in_loaded_modules = False
                    continue

                # === Stack Trace Parsing ===
                # Lines starting with "0x" are addresses
                if in_stack_trace and line.startswith('0x'):
                    try:
                        addr = _parse_hex_address(line)
                        stack_addresses.append(addr)
                    except ValueError:
                        pass  # Skip malformed lines

                # === Loaded Modules Parsing ===
                # Format: 0xbase-0xend /path/to/module
                elif in_loaded_modules and line:
                    try:
                        parts = line.split(None, 1)  # Split on first whitespace
                        if len(parts) == 2:
                            addr_range, path = parts
                            base_str, end_str = addr_range.split('-')
                            base_address = _parse_hex_address(base_str)
                            end_address = _parse_hex_address(end_str)
                            modules.append(Module(base_address, end_address, path))
                    except (ValueError, AttributeError):
                        pass  # Skip malformed lines

    except FileNotFoundError:
        raise FileNotFoundError(f"Crash report not found: {file_path}")
    except Exception as e:
        raise ValueError(f"Failed to parse crash report {file_path}: {e}")

    # Validate that we got at least some data
    if not metadata and not stack_addresses:
        raise ValueError(f"Crash report appears empty or invalid: {file_path}")

    return metadata, stack_addresses, modules


# === High-Level Loading ===

def load_crash_report(file_path: Path) -> CrashReport:
    """
    Load and parse a crash report into a CrashReport object.

    This is the main entry point combining parsing and resolution. It:
    1. Parses the crash report file
    2. Resolves raw addresses to module+offset pairs
    3. Returns a complete CrashReport object

    Args:
        file_path: Path to crash report file

    Returns:
        Fully parsed and resolved CrashReport object

    Raises:
        FileNotFoundError: If file doesn't exist
        ValueError: If file format is invalid
    """
    # Parse the crash report file
    metadata, stack_addresses, modules = parse_crash_report(file_path)

    # Import here to avoid circular dependency
    from .resolver import resolve_stack_trace

    # Resolve addresses to stack frames
    stack_frames = resolve_stack_trace(stack_addresses, modules)

    return CrashReport(
        file_path=file_path,
        metadata=metadata,
        stack_frames=stack_frames,
        modules=modules
    )
