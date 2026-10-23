#!/usr/bin/env python3
# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
import re
import sys
import zipfile
import argparse
from pathlib import Path

# Files that each package must contain, keyed by (platform, linkage), with paths given
# relative to the archive's top-level directory: shared libraries must be accompanied by
# separate debug info (see cmake/debug-info.cmake); static libraries carry their own
REQUIRED_FILES: dict[tuple[str, str], list[str]] = {
    ("linux", "shared"): [
        "lib/libddsdkcpp.so",
        "lib/libddsdkcpp.so.debug",
    ],
    ("linux", "static"): [
        "lib/libddsdkcpp.a",
    ],
    ("macos", "shared"): [
        "lib/libddsdkcpp.dylib",
        "lib/libddsdkcpp.dylib.dSYM/Contents/Resources/DWARF/libddsdkcpp.dylib",
    ],
    ("macos", "static"): [
        "lib/libddsdkcpp.a",
    ],
    ("windows", "shared"): [
        "bin/ddsdkcpp.dll",
        "bin/ddsdkcpp.pdb",
        "lib/ddsdkcpp.lib",
    ],
    ("windows", "static"): [
        "lib/ddsdkcpp.lib",
    ],
}

# Files that a package must not contain: on Windows, a shared build's import library has
# the same name as a static library, so a static package is distinguished by the absence
# of a DLL
FORBIDDEN_FILES: dict[tuple[str, str], list[str]] = {
    ("windows", "static"): [
        "bin/ddsdkcpp.dll",
    ],
}

# Files that every package must contain, regardless of platform and linkage
COMMON_REQUIRED_FILES = [
    "lib/cmake/Datadog/DatadogConfig.cmake",
]


def parse_cmake_bool(value: str) -> bool:
    """Interprets a CMake boolean value such as 'ON' or 'OFF'."""
    if value.upper() in ("ON", "TRUE", "YES", "Y", "1"):
        return True
    if value.upper() in ("OFF", "FALSE", "NO", "N", "0", ""):
        return False
    raise ValueError(f"Not a recognized CMake boolean value: '{value}'")


def list_archive(archive_path: Path) -> list[str]:
    """Returns the paths of all files (not directories) in a .zip archive."""
    with zipfile.ZipFile(archive_path) as archive:
        return [info.filename for info in archive.infolist() if not info.is_dir()]


def main():
    parser = argparse.ArgumentParser(description=(
        "Verifies that the package produced by building the 'package' target is labeled "
        "with the expected linkage and contains the expected library and debug-info files"
    ))
    parser.add_argument("build_dir", type=Path,
                        help="Build directory containing artifact_name.txt and the archive")
    parser.add_argument("--build-shared-libs", required=True,
                        help="Value of BUILD_SHARED_LIBS used to configure the build")
    args = parser.parse_args()

    expected_linkage = "shared" if parse_cmake_bool(args.build_shared_libs) else "static"

    # The package target writes the artifact name (as stamped into the library build) to
    # artifact_name.txt, e.g.
    # 'ddsdkcpp-v0.8.0-linux-x64-clang-libstdc++-shared-release': parse the platform and
    # linkage from it, and verify that the build is labeled with the linkage we asked for
    artifact_name = (args.build_dir / "artifact_name.txt").read_text().strip()
    m = re.fullmatch(r"ddsdkcpp-v[^-]+-(linux|macos|windows)-.+-(shared|static)-[a-z]+",
                     artifact_name)
    if not m:
        sys.exit(f"ERROR: Unrecognized artifact name: '{artifact_name}'")
    platform, linkage = m.group(1), m.group(2)
    if linkage != expected_linkage:
        sys.exit(f"ERROR: Artifact '{artifact_name}' is labeled '{linkage}', but "
                 f"BUILD_SHARED_LIBS={args.build_shared_libs} should produce a "
                 f"'{expected_linkage}' build")

    # CPack writes the archive alongside artifact_name.txt, with all files nested in a
    # top-level directory of the same name
    archive_path = args.build_dir / f"{artifact_name}.zip"
    if not archive_path.is_file():
        sys.exit(f"ERROR: Package not found: {archive_path}")
    prefix = f"{artifact_name}/"
    files = {path.removeprefix(prefix) for path in list_archive(archive_path)}

    # Check for required and forbidden files, reporting all problems at once
    print(f"Checking {archive_path.name} ({len(files)} files):")
    errors: list[str] = []
    for path in COMMON_REQUIRED_FILES + REQUIRED_FILES[(platform, linkage)]:
        if path in files:
            print(f"- OK: {path}")
        else:
            errors.append(f"missing required file: {path}")
    for path in FORBIDDEN_FILES.get((platform, linkage), []):
        if path in files:
            errors.append(f"contains unexpected file: {path}")
    if errors:
        for error in errors:
            print(f"ERROR: {archive_path.name} {error}", file=sys.stderr)
        sys.exit(1)
    print("Package contents OK.")


if __name__ == "__main__":
    main()
