# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Symbolizer registry and factory.

This module provides a registry of all available symbolizer implementations
and factory functions to select and instantiate the appropriate symbolizer
based on platform and user preferences.
"""

import sys
from typing import Optional, Type
from .base import Symbolizer, SymbolizerTool
from .atos import AtosSymbolizer
from .llvm import LlvmSymbolizer
from .addr2line import Addr2lineSymbolizer
from .dbh import DbhSymbolizer


# === Symbolizer Registry ===

# Registry of all symbolizer implementations
_SYMBOLIZERS: list[Type[Symbolizer]] = [
    AtosSymbolizer,
    LlvmSymbolizer,
    Addr2lineSymbolizer,
    DbhSymbolizer,
]


# === Platform Detection ===

def get_default_tool() -> SymbolizerTool:
    """
    Get the default symbolication tool for the current platform.

    Platform defaults:
    - Darwin (macOS): atos
    - Linux: llvm-symbolizer if available, else addr2line
    - Windows: dbh

    Returns:
        SymbolizerTool enum for the platform default

    Raises:
        RuntimeError: If no suitable tool is available for this platform
    """
    platform = sys.platform

    if platform == "darwin":
        # macOS: prefer atos
        if AtosSymbolizer.is_available():
            return SymbolizerTool.ATOS
        # Fall back to llvm-symbolizer if available
        if LlvmSymbolizer.is_available():
            return SymbolizerTool.LLVM
        raise RuntimeError(
            "No symbolication tool available on macOS. "
            "Install atos (from Xcode Command Line Tools) or llvm-symbolizer (from LLVM)."
        )

    elif platform == "linux":
        # Linux: prefer llvm-symbolizer, fall back to addr2line
        if LlvmSymbolizer.is_available():
            return SymbolizerTool.LLVM
        if Addr2lineSymbolizer.is_available():
            return SymbolizerTool.ADDR2LINE
        raise RuntimeError(
            "No symbolication tool available on Linux. "
            "Install llvm-symbolizer (from LLVM) or addr2line (from binutils)."
        )

    elif platform == "win32":
        # Windows: dbh only
        if DbhSymbolizer.is_available():
            return SymbolizerTool.DBH
        raise RuntimeError(
            "dbh.exe not available on Windows. "
            "Install Windows Debugging Tools from Windows SDK or WinDbg Preview."
        )

    else:
        raise RuntimeError(f"Unsupported platform: {platform}")


# === Tool Availability Checking ===

def check_tool_availability(tool: SymbolizerTool) -> tuple[bool, Optional[str]]:
    """
    Check if a symbolication tool is available on this system.

    Args:
        tool: SymbolizerTool to check

    Returns:
        Tuple of (is_available, error_message)
        - If available: (True, None)
        - If not: (False, "helpful error message with installation hints")
    """
    # Handle special cases
    if tool == SymbolizerTool.NONE:
        # 'none' is always available (no symbolication)
        return (True, None)

    if tool == SymbolizerTool.AUTO:
        # AUTO resolves to platform default
        try:
            default_tool = get_default_tool()
            return check_tool_availability(default_tool)
        except RuntimeError as e:
            return (False, str(e))

    # Find the symbolizer implementation for this tool
    symbolizer_cls: Optional[Type[Symbolizer]] = None
    for cls in _SYMBOLIZERS:
        if cls.tool_name() == tool:
            symbolizer_cls = cls
            break

    if symbolizer_cls is None:
        return (False, f"Unknown symbolization tool: {tool}")

    # Check platform compatibility
    platform = sys.platform
    tool_platform = symbolizer_cls.get_platform()

    # llvm-symbolizer is cross-platform, others are platform-specific
    if tool_platform != "cross-platform" and platform != tool_platform:
        platform_name = {
            "darwin": "macOS",
            "linux": "Linux",
            "win32": "Windows"
        }.get(platform, platform)

        tool_platform_name = {
            "darwin": "macOS",
            "linux": "Linux",
            "win32": "Windows"
        }.get(tool_platform, tool_platform)

        return (False, f"{tool} is only available on {tool_platform_name}, but you are on {platform_name}")

    # Check if tool is available
    if not symbolizer_cls.is_available():
        # Generate helpful error message with installation hints
        if tool == SymbolizerTool.ATOS:
            return (False, "atos not found. Install Xcode Command Line Tools: xcode-select --install")
        elif tool == SymbolizerTool.LLVM:
            if platform == "darwin":
                return (False, "llvm-symbolizer not found. Install via: brew install llvm")
            elif platform == "linux":
                return (False, "llvm-symbolizer not found. Install via: apt install llvm (or yum install llvm)")
            else:
                return (False, "llvm-symbolizer not found. Install LLVM toolchain.")
        elif tool == SymbolizerTool.ADDR2LINE:
            return (False, "addr2line not found. Install via: apt install binutils (or yum install binutils)")
        elif tool == SymbolizerTool.DBH:
            return (False,
                    "dbh.exe not found. Install Windows Debugging Tools from:\n"
                    "  - Windows SDK: https://developer.microsoft.com/windows/downloads/windows-sdk/\n"
                    "  - WinDbg Preview from Microsoft Store")
        else:
            return (False, f"{tool} not found in PATH")

    return (True, None)


# === Symbolizer Factory ===

def get_symbolizer(tool: SymbolizerTool) -> Optional[Symbolizer]:
    """
    Get a symbolizer instance for the specified tool.

    This is the main factory function for creating symbolizers.
    It handles AUTO resolution, availability checking, and instantiation.

    Args:
        tool: SymbolizerTool enum value

    Returns:
        Symbolizer instance, or None if tool is NONE

    Raises:
        ValueError: If the tool is not available on this platform
        RuntimeError: If the required tool binary is not found (with helpful error)
    """
    # Handle special case: NONE means no symbolication
    if tool == SymbolizerTool.NONE:
        return None

    # Handle AUTO: resolve to platform default
    if tool == SymbolizerTool.AUTO:
        tool = get_default_tool()

    # Check availability (fail fast with helpful error)
    is_available, error_msg = check_tool_availability(tool)
    if not is_available:
        raise RuntimeError(error_msg)

    # Find and instantiate the symbolizer
    for cls in _SYMBOLIZERS:
        if cls.tool_name() == tool:
            return cls()

    # Should never reach here if check_tool_availability is correct
    raise ValueError(f"No symbolizer implementation found for tool: {tool}")


# Export key types and functions
__all__ = [
    'Symbolizer',
    'SymbolizerTool',
    'get_symbolizer',
    'get_default_tool',
    'check_tool_availability',
]
