#!/usr/bin/env python3
"""
Process crash reports and resolve stack trace addresses to module offsets.
"""

from dataclasses import dataclass
from pathlib import Path
from typing import Optional
import argparse
import os
import sys


@dataclass
class Module:
    """Represents a loaded module/library."""
    base_address: int
    end_address: int
    path: str

    @property
    def name(self) -> str:
        """Extract the module name from the path."""
        return Path(self.path).name


@dataclass
class StackFrame:
    """Represents a resolved stack frame."""
    raw_address: int
    module: Optional[Module] = None
    offset: Optional[int] = None
    frame_number: int = 0

    def format(self, style: str = 'human') -> str:
        """Format the stack frame according to the specified style."""
        if style == 'human':
            if self.module and self.offset is not None:
                return f"0x{self.raw_address:016x} ({self.module.name}+0x{self.offset:x})"
            else:
                return f"0x{self.raw_address:016x}"

        elif style == 'full':
            # Linux kernel / Android tombstone style with full paths
            if self.module and self.offset is not None:
                return (f"#{self.frame_number} 0x{self.raw_address:016x}  "
                       f"{self.module.name} + 0x{self.offset:x}  ({self.module.path})")
            else:
                return f"#{self.frame_number} 0x{self.raw_address:016x}  <unknown>"

        elif style == 'llvm':
            # Format for llvm-symbolizer: echo 'path offset' | llvm-symbolizer
            if self.module and self.offset is not None:
                return f"echo '{self.module.path} 0x{self.offset:x}' | llvm-symbolizer"
            else:
                return f"# Unknown address: 0x{self.raw_address:016x}"

        elif style == 'atos':
            # Format for macOS atos: needs load address and absolute address
            if self.module and self.offset is not None:
                return f"atos -o {self.module.path} -l 0x{self.module.base_address:x} 0x{self.raw_address:x}"
            else:
                return f"# Unknown address: 0x{self.raw_address:016x}"

        elif style == 'addr2line':
            # Format for Linux addr2line: needs binary path and offset
            # -f: show function names, -C: demangle C++, -i: show inlined functions
            if self.module and self.offset is not None:
                return f"addr2line -e {self.module.path} -f -C -i 0x{self.offset:x}"
            else:
                return f"# Unknown address: 0x{self.raw_address:016x}"

        elif style == 'dbh':
            # Format for Windows dbh (Debug Help): needs symbol path, module path, and offset
            if self.module and self.offset is not None:
                symbol_path = get_windows_symbol_path()
                return f'dbh -y "{symbol_path}" -d "{self.module.path}" addr 0x{self.offset:x}'
            else:
                return f"# Unknown address: 0x{self.raw_address:016x}"

        else:
            raise ValueError(f"Unknown format style: {style}")

    def __str__(self) -> str:
        return self.format('human')


def parse_hex_address(addr_str: str) -> int:
    """Parse a hex address string to an integer."""
    return int(addr_str, 16)


def get_windows_symbol_path() -> str:
    """Get the symbol path for Windows debugging tools."""
    # Check environment variable first
    env_path = os.environ.get('_NT_SYMBOL_PATH')
    if env_path:
        return env_path

    # Otherwise use sensible default
    return 'SRV*C:\\Symbols*https://msdl.microsoft.com/download/symbols'


def parse_crash_report(file_path: Path) -> tuple[list[int], list[Module]]:
    """
    Parse a crash report file and extract stack trace addresses and loaded modules.

    Returns:
        Tuple of (stack_addresses, modules)
    """
    stack_addresses: list[int] = []
    modules: list[Module] = []

    in_stack_trace = False
    in_loaded_modules = False

    with open(file_path, 'r') as f:
        for line in f:
            line = line.strip()

            # Skip empty lines
            if not line:
                continue

            # Check for section headers (case-insensitive)
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

            # Parse stack trace addresses
            if in_stack_trace and line.startswith('0x'):
                try:
                    addr = parse_hex_address(line)
                    stack_addresses.append(addr)
                except ValueError:
                    pass  # Skip malformed lines

            # Parse loaded modules
            elif in_loaded_modules and line:
                # Format: 0xbase-0xend /path/to/module
                try:
                    parts = line.split(None, 1)  # Split on first whitespace
                    if len(parts) == 2:
                        addr_range, path = parts
                        base_str, end_str = addr_range.split('-')
                        base_address = parse_hex_address(base_str)
                        end_address = parse_hex_address(end_str)
                        modules.append(Module(base_address, end_address, path))
                except (ValueError, AttributeError):
                    pass  # Skip malformed lines

    return stack_addresses, modules


def resolve_address(address: int, modules: list[Module]) -> StackFrame:
    """
    Resolve a raw address to a module and offset.

    For shared cache scenarios (common on macOS), multiple modules may share
    the same end address. We find the module with the largest base_address
    that is still <= the target address.

    Args:
        address: Raw address to resolve
        modules: List of modules sorted by base address

    Returns:
        StackFrame with resolved information
    """
    candidate: Optional[Module] = None

    # Search through sorted modules to find the one with highest base <= address
    for module in modules:
        if module.base_address <= address:
            # This could be our candidate
            if address < module.end_address:
                candidate = module
            # Keep searching for a better match (higher base address)
        else:
            # Since modules are sorted, we've passed our address
            break

    if candidate:
        offset = address - candidate.base_address
        return StackFrame(address, candidate, offset)

    # Address not found in any module
    return StackFrame(address)


def find_latest_crash_report(crashes_dir: Path) -> Optional[Path]:
    """
    Find the most recent crash report in the .crashes directory.

    Returns:
        Path to the latest crash report, or None if none found
    """
    if not crashes_dir.exists() or not crashes_dir.is_dir():
        return None

    crash_files = sorted(
        crashes_dir.glob('crash_*.txt'),
        reverse=True  # Most recent first (lexical sort)
    )

    return crash_files[0] if crash_files else None


def main() -> int:
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description='Process crash reports and resolve stack trace addresses to module offsets.'
    )
    parser.add_argument(
        'crash_report',
        nargs='?',
        type=Path,
        help='Path to crash report file (default: most recent in .crashes/)'
    )
    parser.add_argument(
        '--format',
        choices=['human', 'full', 'llvm', 'atos', 'addr2line', 'dbh'],
        default='human',
        help='Output format: human (compact), full (with paths), llvm (llvm-symbolizer), atos (macOS atos), addr2line (Linux addr2line), dbh (Windows dbh)'
    )

    args = parser.parse_args()

    # Determine which crash report to process
    crash_report_path: Optional[Path] = args.crash_report

    if crash_report_path is None:
        # Auto-select from .crashes directory
        crashes_dir = Path('.crashes')
        crash_report_path = find_latest_crash_report(crashes_dir)

        if crash_report_path is None:
            print("Error: No crash report specified and none found in .crashes/", file=sys.stderr)
            return 1

        # Only show informational messages for human-readable formats
        if args.format in ['human', 'full']:
            print(f"Using crash report: {crash_report_path}")
            print()

    # Check if file exists
    if not crash_report_path.exists():
        print(f"Error: Crash report not found: {crash_report_path}", file=sys.stderr)
        return 1

    # Parse the crash report
    try:
        stack_addresses, modules = parse_crash_report(crash_report_path)
    except Exception as e:
        print(f"Error parsing crash report: {e}", file=sys.stderr)
        return 1

    # Sort modules by base address
    modules.sort(key=lambda m: m.base_address)

    # Report findings (only in human-readable formats)
    if args.format in ['human', 'full']:
        print(f"Found {len(stack_addresses)} stack frames")
        print(f"Found {len(modules)} loaded modules")
        print()

    # Resolve and print stack trace
    if args.format in ['human', 'full']:
        print("Resolved stack trace:")
        print("-" * 80)

    for i, addr in enumerate(stack_addresses):
        frame = resolve_address(addr, modules)
        frame.frame_number = i
        print(frame.format(args.format))

    if args.format in ['human', 'full']:
        print("-" * 80)

    return 0


if __name__ == '__main__':
    sys.exit(main())
