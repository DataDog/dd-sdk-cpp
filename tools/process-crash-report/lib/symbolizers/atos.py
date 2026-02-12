# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
macOS atos symbolizer implementation.

The atos tool is Apple's native symbolication utility for macOS/iOS binaries.
It requires the binary path, load address, and raw address to symbolicate.
"""

import shutil
import subprocess
import sys
from .base import Symbolizer, SymbolizerTool
from ..models import StackFrame, SymbolizedFrame


class AtosSymbolizer(Symbolizer):
    """
    Symbolizer implementation using macOS atos.

    Command format:
        atos -o <binary_path> -l <load_address> <raw_address>

    The tool outputs a single line with function name and source location,
    or just the address in hex if symbolication fails.
    """

    @classmethod
    def tool_name(cls) -> SymbolizerTool:
        """Return ATOS tool identifier."""
        return SymbolizerTool.ATOS

    @classmethod
    def is_available(cls) -> bool:
        """Check if atos is available in PATH."""
        return shutil.which('atos') is not None

    @classmethod
    def get_platform(cls) -> str:
        """atos is only available on macOS."""
        return "darwin"

    def symbolicate_frame(self, frame: StackFrame) -> SymbolizedFrame:
        """
        Symbolicate a single frame using atos.

        Runs: atos -o <module.path> -l <module.base_address> <raw_address>

        Example output:
            "_start (in dd_native_repl) + 52"
            "malloc_zone_malloc (in libsystem_malloc.dylib) (malloc.c:123)"

        If symbolication fails, atos outputs the hex address:
            "0x00007fff12345678"

        Args:
            frame: StackFrame to symbolicate

        Returns:
            SymbolizedFrame with function name and location
        """
        # Handle unresolved frames
        if not frame.is_resolved or frame.module is None:
            return SymbolizedFrame(
                frame_number=frame.frame_number,
                function="??",
                location="??:?"
            )

        try:
            # Build atos command
            # Format: atos -o <binary> -l <load_address> <raw_address>
            cmd = [
                'atos',
                '-o', frame.module.path,
                '-l', f'0x{frame.module.base_address:x}',
                f'0x{frame.raw_address:x}'
            ]

            # Run atos and capture output
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

            # Parse output
            output = result.stdout.strip()

            if not output or output.startswith('0x'):
                # Symbolication failed (got hex address back)
                return SymbolizedFrame(
                    frame_number=frame.frame_number,
                    function="??",
                    location="??:?"
                )

            # Parse atos output format
            # Examples:
            #   "_start (in dd_native_repl) + 52"
            #   "malloc_zone_malloc (in libsystem_malloc.dylib) (malloc.c:123)"
            #   "foo::bar() (foo.cpp:42)"

            function = "??"
            location = "??:?"

            # Try to extract function name and location
            # Strategy: function name is before " (in " or first "("
            if " (in " in output:
                # Has " (in module)" part
                function = output.split(" (in ")[0].strip()
                # Location might be after the module name in parens
                if ") (" in output:
                    # Format: "func (in module) (file:line)"
                    location_part = output.split(") (", 1)[1].rstrip(")")
                    location = location_part
            elif "(" in output:
                # Has parens but no " (in "
                # Could be: "func (file:line)" or "func() + offset"
                parts = output.split("(", 1)
                function = parts[0].strip()
                rest = parts[1].rstrip(")")
                # Check if rest looks like a file:line
                if ":" in rest and not " + " in rest:
                    location = rest
                # Otherwise it's probably "file.cpp:line)" format
                elif ")" in rest:
                    location = rest.split(")")[0]
            else:
                # Just a function name
                function = output.strip()

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
