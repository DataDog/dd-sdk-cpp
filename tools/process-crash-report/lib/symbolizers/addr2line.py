# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
GNU addr2line symbolizer implementation (Linux/Unix).

The addr2line tool is part of GNU binutils and is commonly available on Linux
systems. It requires the binary path and offset, and outputs function name and
source location on separate lines.
"""

import shutil
import subprocess
from .base import Symbolizer, SymbolizerTool
from ..models import StackFrame, SymbolizedFrame


class Addr2lineSymbolizer(Symbolizer):
    """
    Symbolizer implementation using addr2line.

    Command format:
        addr2line -e <binary_path> -f -C -i <offset>

    Flags:
        -f: Show function names
        -C: Demangle C++ symbols
        -i: Show inlined functions

    Output format (two lines):
        <function_name>
        <file>:<line>

    If symbolication fails, outputs:
        ??
        ??:0 or ??:?
    """

    @classmethod
    def tool_name(cls) -> SymbolizerTool:
        """Return ADDR2LINE tool identifier."""
        return SymbolizerTool.ADDR2LINE

    @classmethod
    def is_available(cls) -> bool:
        """Check if addr2line is available in PATH."""
        return shutil.which('addr2line') is not None

    @classmethod
    def get_platform(cls) -> str:
        """addr2line is primarily for Linux/Unix systems."""
        return "linux"

    def symbolicate_frame(self, frame: StackFrame) -> SymbolizedFrame:
        """
        Symbolicate a single frame using addr2line.

        Runs: addr2line -e <module.path> -f -C -i 0x<offset>

        Example output:
            main
            main.cpp:42

        Args:
            frame: StackFrame to symbolicate

        Returns:
            SymbolizedFrame with function name and location
        """
        # Handle unresolved frames
        if not frame.is_resolved or frame.module is None or frame.offset is None:
            return SymbolizedFrame(
                frame_number=frame.frame_number,
                function="??",
                location="??:?"
            )

        try:
            # Build addr2line command
            # Format: addr2line -e <binary> -f -C -i <offset>
            cmd = [
                'addr2line',
                '-e', frame.module.path,
                '-f',  # Show function names
                '-C',  # Demangle C++ symbols
                '-i',  # Show inlined functions
                f'0x{frame.offset:x}'
            ]

            # Run addr2line and capture output
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=10
            )

            if result.returncode != 0:
                # Command failed
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )

            # Parse output (two lines: function, then file:line)
            lines = result.stdout.strip().split('\n')

            if len(lines) < 2:
                # Unexpected output format
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )

            function = lines[0].strip()
            location = lines[1].strip()

            # Check for failure markers
            if function == "??" or location.startswith("??"):
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )

            return SymbolizedFrame(
                frame_number=frame.frame_number,
                function=function,
                location=location
            )

        except (subprocess.TimeoutExpired, subprocess.SubprocessError, Exception):
            # Best effort: return unknown on any error
            return SymbolizedFrame(
                frame_number=frame.frame_number,
                function="??",
                location="??:?"
            )
