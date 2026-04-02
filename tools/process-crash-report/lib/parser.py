# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Parse crash report files into structured data.

This module handles reading binary crash report files from disk, parsing their
sections (header, modules, stack frames), and combining parsed data with address
resolution to produce complete CrashReport objects.

The binary format is defined in crash_report.hpp and uses little-endian uint64_t
values with magic constants to identify sections.
"""

import sys
import struct
import signal
from pathlib import Path
from datetime import datetime, timezone
from typing import Optional
from .models import CrashReport, Module, RumContext


# === Magic Constants ===
# These match the constants defined in crash_report.hpp

CRASH_REPORT_HEADER_MAGIC = 0xdd01
CRASH_REPORT_FILE_VERSION = 1
CRASH_REPORT_MODULE_MAGIC = 0xdda1
CRASH_REPORT_STACK_FRAME_MAGIC = 0xdda2
CRASH_REPORT_FOOTER_MAGIC = 0xddff

# Size constants (in bytes)
UINT64_SIZE = 8
HEADER_SIZE = 64  # 8 uint64_t values

# Magic constants for .ctx files (crash_context.hpp)
CRASH_CONTEXT_HEADER_MAGIC = 0xdc01
CRASH_CONTEXT_FILE_VERSION = 1
CRASH_CONTEXT_FOOTER_MAGIC = 0xdcff

# .ctx file size: 2× uint64_t (header magic + version) + 4× 16-byte UUID + 1× uint64_t (footer)
CRASH_CONTEXT_FILE_SIZE = 2 * UINT64_SIZE + 4 * 16 + UINT64_SIZE


# === Platform-Specific Signal/Exception Mapping ===

def _get_signal_name(fault_code: int) -> Optional[str]:
    """
    Map a POSIX signal number to its name.

    Args:
        fault_code: Signal number

    Returns:
        Signal name (e.g., "SIGSEGV") or None if unknown
    """
    # Build reverse mapping from signal numbers to names
    signal_map = {
        signal.SIGSEGV: "SIGSEGV",
        signal.SIGBUS: "SIGBUS",
        signal.SIGILL: "SIGILL",
        signal.SIGFPE: "SIGFPE",
        signal.SIGABRT: "SIGABRT",
        signal.SIGTRAP: "SIGTRAP",
    }

    return signal_map.get(fault_code)


def _format_fault_code(fault_code: int) -> str:
    """
    Format the fault code appropriately for the platform.

    On POSIX systems (macOS, Linux), formats as signal name + number.
    On Windows, formats as hex exception code.

    Args:
        fault_code: Signal number (POSIX) or exception code (Windows)

    Returns:
        Formatted string like "Signal: SIGSEGV (11)" or "Exception Code: 0xc0000005"
    """
    if sys.platform == 'win32':
        # Windows: Format as hex exception code
        return f"Exception Code: 0x{fault_code:08x}"
    else:
        # POSIX: Try to map to signal name, fallback to number
        signal_name = _get_signal_name(fault_code)
        if signal_name:
            return f"Signal: {signal_name} ({fault_code})"
        else:
            return f"Signal: {fault_code}"


def _format_timestamp(timestamp: int) -> str:
    """
    Format Unix timestamp with human-readable date/time.

    Args:
        timestamp: Unix timestamp (seconds since epoch)

    Returns:
        Formatted string like "1770922852 (2026-02-12 18:14:12 UTC)"
    """
    try:
        dt = datetime.fromtimestamp(timestamp, tz=timezone.utc)
        human_readable = dt.strftime('%Y-%m-%d %H:%M:%S UTC')
        return f"{timestamp} ({human_readable})"
    except (ValueError, OSError):
        # Timestamp out of range or invalid
        return str(timestamp)


# === Crash Context Parsing ===

def _uuid_bytes_to_str(data: bytes) -> Optional[str]:
    """
    Convert 16 raw UUID bytes to the standard string representation.

    Returns None if all bytes are zero (indicating "not set"), otherwise
    formats as xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx using the byte order
    defined by RFC 4122 (bytes 0-3, 4-5, 6-7, 8-9, 10-15).
    """
    if all(b == 0 for b in data):
        return None
    hex_str = data.hex()
    return f"{hex_str[0:8]}-{hex_str[8:12]}-{hex_str[12:16]}-{hex_str[16:20]}-{hex_str[20:32]}"


def parse_crash_context(file_path: Path) -> Optional[RumContext]:
    """
    Parse a crash context (.ctx) file produced by the SDK's crash handler.

    The .ctx file is written atomically whenever the active RUM context changes
    and is deleted on clean shutdown. It contains the RUM session identifiers
    (application, session, view, and action IDs) active at the time of the crash.

    The binary format is defined in crash_context.hpp:
      0x00  8B  header magic  (0xdc01, little-endian uint64_t)
      0x08  8B  version       (0x0001, little-endian uint64_t)
      0x10  16B application_id UUID (raw bytes)
      0x20  16B session_id UUID
      0x30  16B view_id UUID
      0x40  16B action_id UUID
      0x50  8B  footer magic  (0xdcff, little-endian uint64_t)

    Returns None (without raising) if the file is absent, has an unexpected
    size, or contains an invalid magic number. UUID fields that are all-zeros
    are represented as None in the returned RumContext.
    """
    try:
        data = file_path.read_bytes()
    except FileNotFoundError:
        return None
    except OSError:
        return None

    if len(data) != CRASH_CONTEXT_FILE_SIZE:
        print(
            f"Warning: .ctx file has unexpected size "
            f"(expected {CRASH_CONTEXT_FILE_SIZE}, got {len(data)}): {file_path}",
            file=sys.stderr
        )
        return None

    header_magic, version = struct.unpack_from('<2Q', data, 0)
    if header_magic != CRASH_CONTEXT_HEADER_MAGIC:
        print(
            f"Warning: .ctx file has invalid header magic "
            f"(expected 0x{CRASH_CONTEXT_HEADER_MAGIC:x}, got 0x{header_magic:x}): {file_path}",
            file=sys.stderr
        )
        return None

    footer_magic, = struct.unpack_from('<Q', data, CRASH_CONTEXT_FILE_SIZE - UINT64_SIZE)
    if footer_magic != CRASH_CONTEXT_FOOTER_MAGIC:
        print(
            f"Warning: .ctx file has invalid footer magic "
            f"(expected 0x{CRASH_CONTEXT_FOOTER_MAGIC:x}, got 0x{footer_magic:x}): {file_path}",
            file=sys.stderr
        )
        return None

    offset = 2 * UINT64_SIZE  # skip header magic + version
    application_id = _uuid_bytes_to_str(data[offset:offset + 16]); offset += 16
    session_id = _uuid_bytes_to_str(data[offset:offset + 16]); offset += 16
    view_id = _uuid_bytes_to_str(data[offset:offset + 16]); offset += 16
    action_id = _uuid_bytes_to_str(data[offset:offset + 16])

    return RumContext(
        application_id=application_id,
        session_id=session_id,
        view_id=view_id,
        action_id=action_id,
    )


# === Crash Report Discovery ===

def find_latest_crash_report(crashes_dir: Path) -> Optional[Path]:
    """
    Find the most recent crash report in the crashes directory.

    Crash reports are named crash_<timestamp>_<pid>, where timestamp
    uses lexicographically sortable format. This function finds the latest
    by sorting in reverse order.

    Args:
        crashes_dir: Directory to search (typically .crashes/)

    Returns:
        Path to most recent crash_* file, or None if none found or
        directory doesn't exist
    """
    if not crashes_dir.exists() or not crashes_dir.is_dir():
        return None

    crash_files = sorted(
        (p for p in crashes_dir.glob('crash_*') if not p.suffix),
        reverse=True  # Most recent first (lexical sort)
    )

    return crash_files[0] if crash_files else None


# === Crash Report Parsing ===

def parse_crash_report(file_path: Path) -> tuple[dict[str, str], list[int], list[Module]]:
    """
    Parse a binary crash report file into components.

    The binary format is defined in crash_report.hpp and consists of:
    1. Header section: magic, version, and crash metadata (64 bytes)
    2. Module section: repeated module entries with magic, addresses, and paths
    3. Stack frame section: repeated frame entries with magic and addresses
    4. Footer: footer magic constant

    All values are uint64_t (8 bytes) in little-endian byte order.

    Args:
        file_path: Path to binary crash report file

    Returns:
        Tuple of (metadata_dict, stack_addresses, modules)
        - metadata_dict: Key-value pairs from header section
        - stack_addresses: Raw addresses from stack frame section (in order)
        - modules: Parsed Module objects from module section

    Raises:
        FileNotFoundError: If file doesn't exist
        ValueError: If file format is invalid or cannot be parsed
    """
    metadata: dict[str, str] = {}
    stack_addresses: list[int] = []
    modules: list[Module] = []

    try:
        with open(file_path, 'rb') as f:
            # === Parse Header (64 bytes) ===
            header_data = f.read(HEADER_SIZE)
            if len(header_data) < HEADER_SIZE:
                raise ValueError("Truncated crash report file (incomplete header)")

            # Unpack 8 uint64_t values in little-endian format
            header_values = struct.unpack('<8Q', header_data)
            (magic, version, fault_code, fault_address, fault_flags,
             pid, tid, timestamp) = header_values

            # Validate magic number
            if magic != CRASH_REPORT_HEADER_MAGIC:
                raise ValueError(
                    f"Invalid crash report: bad magic "
                    f"(expected 0x{CRASH_REPORT_HEADER_MAGIC:x}, got 0x{magic:x})"
                )

            # Check version (warn but continue if != 1)
            if version != CRASH_REPORT_FILE_VERSION:
                print(
                    f"Warning: Unknown crash report version {version} "
                    f"(expected {CRASH_REPORT_FILE_VERSION}), attempting parse",
                    file=sys.stderr
                )

            # Build metadata dict
            metadata[_format_fault_code(fault_code).split(':')[0]] = \
                _format_fault_code(fault_code).split(': ', 1)[1]
            metadata["Fault Address"] = f"0x{fault_address:016x}"
            metadata["PID"] = str(pid)
            metadata["TID"] = f"0x{tid:x}"
            metadata["Timestamp"] = _format_timestamp(timestamp)

            # Include fault_flags if non-zero (Windows only)
            if fault_flags != 0:
                metadata["Fault Flags"] = f"0x{fault_flags:x}"

            # === Parse Modules and Stack Frames ===
            # Read magic values and dispatch to appropriate handler

            while True:
                # Read next magic constant
                magic_data = f.read(UINT64_SIZE)
                if len(magic_data) < UINT64_SIZE:
                    raise ValueError("Truncated crash report file (incomplete section magic)")

                magic = struct.unpack('<Q', magic_data)[0]

                if magic == CRASH_REPORT_MODULE_MAGIC:
                    # Parse module entry: start_addr, end_addr
                    module_data = f.read(2 * UINT64_SIZE)
                    if len(module_data) < 2 * UINT64_SIZE:
                        raise ValueError("Truncated crash report file (incomplete module header)")

                    start_addr, end_addr = struct.unpack('<2Q', module_data)

                    # Read path as length-prefixed string
                    path_len_data = f.read(UINT64_SIZE)
                    if len(path_len_data) < UINT64_SIZE:
                        raise ValueError("Truncated crash report file (missing path length)")

                    num_path_bytes = struct.unpack('<Q', path_len_data)[0]

                    path_data = f.read(num_path_bytes)
                    if len(path_data) < num_path_bytes:
                        raise ValueError("Truncated crash report file (incomplete module path)")

                    try:
                        path = path_data.decode('utf-8')
                    except UnicodeDecodeError:
                        # Use replacement characters for invalid UTF-8
                        path = path_data.decode('utf-8', errors='replace')

                    # Read build ID as length-prefixed string
                    buildid_len_data = f.read(UINT64_SIZE)
                    if len(buildid_len_data) < UINT64_SIZE:
                        raise ValueError("Truncated crash report file (missing build ID length)")

                    num_buildid_bytes = struct.unpack('<Q', buildid_len_data)[0]

                    build_id = ""
                    if num_buildid_bytes > 0:
                        buildid_data = f.read(num_buildid_bytes)
                        if len(buildid_data) < num_buildid_bytes:
                            raise ValueError("Truncated crash report file (incomplete build ID)")

                        try:
                            build_id = buildid_data.decode('utf-8')
                        except UnicodeDecodeError:
                            # Use replacement characters for invalid UTF-8
                            build_id = buildid_data.decode('utf-8', errors='replace')

                    modules.append(Module(start_addr, end_addr, path, build_id))

                elif magic == CRASH_REPORT_STACK_FRAME_MAGIC:
                    # Parse stack frame entry
                    frame_data = f.read(UINT64_SIZE)
                    if len(frame_data) < UINT64_SIZE:
                        raise ValueError("Truncated crash report file (incomplete stack frame)")

                    raw_address = struct.unpack('<Q', frame_data)[0]
                    stack_addresses.append(raw_address)

                elif magic == CRASH_REPORT_FOOTER_MAGIC:
                    # End of file
                    break

                else:
                    # Unknown magic value
                    raise ValueError(
                        f"Invalid crash report: unexpected magic 0x{magic:x} "
                        f"at offset {f.tell() - UINT64_SIZE}"
                    )

    except FileNotFoundError:
        raise FileNotFoundError(f"Crash report not found: {file_path}")
    except struct.error as e:
        raise ValueError(f"Truncated crash report file: {e}")
    except Exception as e:
        if isinstance(e, ValueError):
            raise
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

    # Load the accompanying .ctx file if present
    rum_context = parse_crash_context(file_path.with_suffix('.ctx'))

    return CrashReport(
        file_path=file_path,
        metadata=metadata,
        stack_frames=stack_frames,
        modules=modules,
        rum_context=rum_context,
    )
