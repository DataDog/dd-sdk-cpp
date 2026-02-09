# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Base symbolizer interface and common types.

This module defines the abstract base class that all symbolizers must implement,
along with enums for symbolization tools. The Symbolizer ABC provides a common
interface for different platform-specific symbolication backends.
"""

from abc import ABC, abstractmethod
from enum import Enum
from ..models import StackFrame, SymbolizedFrame


class SymbolizerTool(str, Enum):
    """
    Supported symbolication tools.

    Each tool corresponds to a specific symbolizer implementation:
    - ATOS: macOS atos (Apple tool symbolizer)
    - LLVM: llvm-symbolizer (cross-platform LLVM tool)
    - ADDR2LINE: addr2line (GNU binutils, common on Linux)
    - DBH: dbh.exe (Windows Debugging Tools)
    - NONE: No symbolication, just show resolved addresses
    - AUTO: Automatically select platform default
    """
    ATOS = "atos"
    LLVM = "llvm-symbolizer"
    ADDR2LINE = "addr2line"
    DBH = "dbh"
    NONE = "none"
    AUTO = "auto"


class Symbolizer(ABC):
    """
    Abstract base class for symbolization backends.

    Each symbolizer implementation (atos, llvm-symbolizer, addr2line, dbh)
    must subclass this and implement all abstract methods. The interface
    provides both single-frame and batch symbolication capabilities.
    """

    @classmethod
    @abstractmethod
    def tool_name(cls) -> SymbolizerTool:
        """
        Return the tool identifier for this symbolizer.

        Returns:
            SymbolizerTool enum value
        """
        pass

    @classmethod
    @abstractmethod
    def is_available(cls) -> bool:
        """
        Check if this symbolizer's tool is available on the system.

        This should check for the presence of the required binary (e.g., using
        shutil.which) and return True only if the tool can be executed.

        Returns:
            True if the tool is available and can be used, False otherwise
        """
        pass

    @classmethod
    @abstractmethod
    def get_platform(cls) -> str:
        """
        Return the platform this symbolizer is designed for.

        Valid platforms match sys.platform values:
        - "darwin": macOS
        - "linux": Linux
        - "win32": Windows

        Returns:
            Platform string
        """
        pass

    @abstractmethod
    def symbolicate_frame(self, frame: StackFrame) -> SymbolizedFrame:
        """
        Symbolicate a single stack frame.

        This is the core method that each symbolizer must implement. It takes
        a resolved StackFrame (with module and offset) and returns a
        SymbolizedFrame with function name and source location.

        For unresolved frames (no module), implementations should return
        a SymbolizedFrame with function="??" and location="??:?".

        For frames where symbolication fails (tool error, missing symbols),
        implementations should return a SymbolizedFrame with function="??"
        and location="??:?" rather than raising an exception (best-effort).

        Args:
            frame: Resolved StackFrame to symbolicate

        Returns:
            SymbolizedFrame with function name and location
            (or "??" / "??:?" if symbolication fails)
        """
        pass

    def symbolicate_stack(self, frames: list[StackFrame]) -> list[SymbolizedFrame]:
        """
        Symbolicate a list of frames.

        Default implementation calls symbolicate_frame for each frame.
        Subclasses can override this method to implement batch processing
        for better performance (e.g., piping all frames to llvm-symbolizer
        in a single invocation).

        Args:
            frames: List of StackFrame objects

        Returns:
            List of SymbolizedFrame objects (same order as input)
        """
        return [self.symbolicate_frame(frame) for frame in frames]
