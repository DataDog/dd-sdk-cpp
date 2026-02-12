# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Windows DBH symbolizer implementation.

The dbh.exe tool is part of Windows Debugging Tools and is used for symbol
resolution on Windows. It requires special handling for Symbol Server paths
and uses ImageBase addresses for symbolication.
"""

import os
import shutil
import subprocess
from typing import Optional
from .base import Symbolizer, SymbolizerTool
from ..models import StackFrame, SymbolizedFrame


class DbhSymbolizer(Symbolizer):
    """
    Symbolizer implementation using Windows dbh.exe.

    DBH requires special handling:
    1. Symbol Server path configuration (_NT_SYMBOL_PATH)
    2. Module ImageBase resolution (via 'dbh info' command)
    3. Address calculation: ImageBase + offset
    4. Output parsing (skip headers and indented lines)

    Command format:
        dbh -s:"<symbol_path>" "<module_path>" addr <dbh_address>

    The ImageBase is cached per module to avoid repeated 'info' queries.
    Note that frame.offset uses the crash-time base address, and we add
    the ImageBase adjustment only during symbolication.
    """

    def __init__(self):
        """Initialize the DBH symbolizer with caching."""
        self._dbh_exe_path: Optional[str] = None
        self._base_address_cache: dict[str, int] = {}

    @classmethod
    def tool_name(cls) -> SymbolizerTool:
        """Return DBH tool identifier."""
        return SymbolizerTool.DBH

    @classmethod
    def is_available(cls) -> bool:
        """Check if dbh.exe is available."""
        return cls._resolve_dbh_exe() is not None

    @classmethod
    def get_platform(cls) -> str:
        """dbh.exe is only available on Windows."""
        return "win32"

    @staticmethod
    def _resolve_dbh_exe() -> Optional[str]:
        """
        Resolve the path to dbh.exe.

        Checks:
        1. PATH environment variable
        2. Standard Windows Debugging Tools location

        Returns:
            Path to dbh.exe if found, None otherwise
        """
        # First check if dbh.exe is in PATH
        dbh_path = shutil.which('dbh.exe')
        if dbh_path:
            return dbh_path

        # Check standard Windows Debugging Tools location
        program_files_x86 = os.environ.get('ProgramFiles(x86)', 'C:\\Program Files (x86)')
        standard_path = os.path.join(
            program_files_x86,
            'Windows Kits',
            '10',
            'Debuggers',
            'x64',
            'dbh.exe'
        )

        if os.path.isfile(standard_path):
            return standard_path

        return None

    @staticmethod
    def _get_symbol_path() -> str:
        """
        Get the symbol path for Windows debugging tools.

        Reads from _NT_SYMBOL_PATH environment variable if set,
        otherwise uses Microsoft's symbol server as default.

        Returns:
            Symbol server path string for dbh -s: flag
        """
        # Check environment variable first
        env_path = os.environ.get('_NT_SYMBOL_PATH')
        if env_path:
            return env_path

        # Otherwise use sensible default
        return 'SRV*C:\\Symbols*https://msdl.microsoft.com/download/symbols'

    def _get_base_address(self, module_path: str) -> Optional[int]:
        """
        Get the ImageBase address that DBH uses for a module.

        Runs 'dbh.exe <module> info' and parses output for ImageBase.

        The ImageBase is the address where DBH internally loads the module
        for symbolication, which may differ from the crash-time base address.

        Args:
            module_path: Path to the module to query

        Returns:
            ImageBase address as integer, or None if unable to determine
        """
        if self._dbh_exe_path is None:
            return None

        try:
            # Run: dbh.exe "module_path" info
            result = subprocess.run(
                [self._dbh_exe_path, module_path, 'info'],
                capture_output=True,
                text=True,
                timeout=10
            )

            if result.returncode != 0:
                return None

            # Parse output looking for: BaseOfImage : 0xADDRESS
            for line in result.stdout.splitlines():
                line = line.strip()
                if line.startswith('BaseOfImage'):
                    # Format: "BaseOfImage : 0x01000000"
                    parts = line.split(':', 1)
                    if len(parts) == 2:
                        addr_str = parts[1].strip()
                        if addr_str.startswith('0x'):
                            return int(addr_str, 16)

            return None

        except (subprocess.TimeoutExpired, subprocess.SubprocessError, ValueError):
            return None

    def symbolicate_frame(self, frame: StackFrame) -> SymbolizedFrame:
        """
        Symbolicate a single frame using dbh.exe.

        Process:
        1. Resolve dbh.exe path (cached)
        2. Get module ImageBase (cached per module)
        3. Calculate DBH address: ImageBase + frame.offset
        4. Run: dbh -s:"<symbol_path>" "<module>" addr <address>
        5. Parse output (skip headers and indented lines)

        DBH output format:
            Symbol Search Path: <path>

            <indented module info>
            <symbol_name>

        We want the first non-indented, non-header line (the symbol).

        Args:
            frame: StackFrame to symbolicate

        Returns:
            SymbolizedFrame with function name (location is "??:?" since
            DBH doesn't provide file:line info)
        """
        # Handle unresolved frames
        if not frame.is_resolved or frame.module is None or frame.offset is None:
            return SymbolizedFrame(
                frame_number=frame.frame_number,
                function="??",
                location="??:?"
            )

        # Resolve dbh.exe path (cached)
        if self._dbh_exe_path is None:
            self._dbh_exe_path = self._resolve_dbh_exe()
            if self._dbh_exe_path is None:
                # dbh.exe not found
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )

        # Get ImageBase for this module (cached)
        module_path = frame.module.path
        if module_path not in self._base_address_cache:
            base_addr = self._get_base_address(module_path)
            if base_addr is None:
                # Unable to determine ImageBase
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )
            self._base_address_cache[module_path] = base_addr

        # Calculate DBH address: ImageBase + offset
        dbh_base = self._base_address_cache[module_path]
        dbh_address = dbh_base + frame.offset

        try:
            # Build dbh command
            symbol_path = self._get_symbol_path()
            cmd = [
                self._dbh_exe_path,
                f'-s:{symbol_path}',
                module_path,
                'addr',
                f'0x{dbh_address:x}'
            ]

            # Run dbh and capture output
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=30  # DBH can be slow with symbol servers
            )

            if result.returncode != 0:
                # Command failed
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )

            # Parse output
            # Skip:
            # 1. Lines starting with "Symbol Search Path:"
            # 2. Indented lines (start with space)
            # Extract: First non-indented, non-header line
            symbol_name = None
            for line in result.stdout.splitlines():
                # Skip empty lines
                if not line or not line.strip():
                    continue

                # Skip "Symbol Search Path:" header
                if line.strip().startswith("Symbol Search Path:"):
                    continue

                # Skip indented lines
                if line.startswith(' ') or line.startswith('\t'):
                    continue

                # This is a non-indented, non-header line - likely the symbol
                symbol_name = line.strip()
                break

            if not symbol_name:
                # No symbol found
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )

            # DBH doesn't provide file:line info, only symbol names
            return SymbolizedFrame(
                frame_number=frame.frame_number,
                function=symbol_name,
                location="??:?"  # DBH doesn't provide source location
            )

        except (subprocess.TimeoutExpired, subprocess.SubprocessError, Exception):
            # Best effort: return unknown on any error
            return SymbolizedFrame(
                frame_number=frame.frame_number,
                function="??",
                location="??:?"
            )
