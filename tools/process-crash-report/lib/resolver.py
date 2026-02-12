# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
Resolve raw addresses to module+offset pairs.

This module handles the core logic of mapping raw memory addresses from crash
reports to specific loaded modules and calculating offsets within those modules.
This is necessary before symbolication can occur.
"""

from typing import Optional
from .models import Module, StackFrame


def resolve_address(address: int, modules: list[Module]) -> StackFrame:
    """
    Resolve a raw address to a module and offset.

    For shared cache scenarios (common on macOS), multiple modules may share
    the same end address. We find the module with the largest base_address
    that is still <= the target address.

    Implementation strategy:
    - Assumes modules are sorted by base_address (ascending)
    - Iterates through modules looking for base_address <= address < end_address
    - Keeps searching to find highest matching base (handles overlapping ranges)
    - Once we pass the address (base > address), we stop since list is sorted

    Args:
        address: Raw address to resolve
        modules: List of modules sorted by base_address (ascending)

    Returns:
        StackFrame with resolved information (or unresolved if no match)
        - If resolved: contains module and offset
        - If unresolved: contains only raw_address
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
        # Calculate offset within the module
        # This is the unmodified offset from crash-time base address
        # For DBH on Windows, ImageBase adjustment happens during symbolication
        offset = address - candidate.base_address
        return StackFrame(address, candidate, offset)

    # Address not found in any module
    return StackFrame(address)


def resolve_stack_trace(addresses: list[int], modules: list[Module]) -> list[StackFrame]:
    """
    Resolve a list of addresses to stack frames.

    This is the main entry point for resolving an entire stack trace.
    It ensures modules are properly sorted and resolves each address
    with appropriate frame numbering.

    Args:
        addresses: Raw hex addresses from stack trace (in stack order)
        modules: Loaded modules (will be sorted by base_address internally)

    Returns:
        List of StackFrame objects with frame_number set (0-indexed)
    """
    # Ensure modules are sorted by base_address for efficient resolution
    sorted_modules = sorted(modules, key=lambda m: m.base_address)

    # Resolve each address and assign frame numbers
    stack_frames: list[StackFrame] = []
    for frame_num, address in enumerate(addresses):
        frame = resolve_address(address, sorted_modules)
        frame.frame_number = frame_num
        stack_frames.append(frame)

    return stack_frames
