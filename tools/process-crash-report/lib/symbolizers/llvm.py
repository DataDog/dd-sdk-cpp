# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
LLVM symbolizer implementation (cross-platform).

The llvm-symbolizer tool is part of the LLVM toolchain and works on macOS,
Linux, and Windows. It uses a piped input format where you send module path
and offset, and it returns function name and source location on separate lines.
"""

import shutil
import subprocess
from .base import Symbolizer, SymbolizerTool
from ..models import StackFrame, SymbolizedFrame


class LlvmSymbolizer(Symbolizer):
    """
    Symbolizer implementation using llvm-symbolizer.

    Input format (piped to stdin):
        <binary_path> <offset>

    Output format (two lines):
        <function_name>
        <file>:<line>:<column>

    If symbolication fails, outputs:
        ??
        ??:0:0
    """

    @classmethod
    def tool_name(cls) -> SymbolizerTool:
        """Return LLVM tool identifier."""
        return SymbolizerTool.LLVM

    @classmethod
    def is_available(cls) -> bool:
        """Check if llvm-symbolizer is available in PATH."""
        return shutil.which('llvm-symbolizer') is not None

    @classmethod
    def get_platform(cls) -> str:
        """llvm-symbolizer is cross-platform (works everywhere)."""
        # Return a placeholder - registry will check actual platform at runtime
        return "cross-platform"

    def symbolicate_frame(self, frame: StackFrame) -> SymbolizedFrame:
        """
        Symbolicate a single frame using llvm-symbolizer.

        Pipes input: "<module_path> 0x<offset>"
        Reads output: two lines (function name, then file:line:col)

        Example:
            Input:  /path/to/binary 0x1234
            Output: main
                    main.cpp:42:5

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
            # Build input string: "<module_path> <offset>"
            input_str = f"{frame.module.path} 0x{frame.offset:x}\n"

            # Run llvm-symbolizer with piped input
            result = subprocess.run(
                ['llvm-symbolizer'],
                input=input_str,
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

            # Parse output (two lines)
            lines = result.stdout.strip().split('\n')

            if len(lines) < 2:
                # Unexpected output format
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )

            function = lines[0].strip()
            location_line = lines[1].strip()

            # Check for failure markers
            if function == "??" or location_line.startswith("??:"):
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )

            # Parse location (format: file:line:col or file:line)
            # We want just "file:line" for consistency
            location = location_line
            if location.count(':') >= 2:
                # Has column number, strip it
                parts = location.rsplit(':', 1)
                location = parts[0]

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
