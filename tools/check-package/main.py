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
    "LICENSE.md",
    "NOTICE",
]

# Third-party license texts are installed to licenses/
LICENSES_DIR = "licenses/"


def parse_cmake_bool(value: str) -> bool:
    """Interprets a CMake boolean value such as 'ON' or 'OFF'."""
    if value.upper() in ("ON", "TRUE", "YES", "Y", "1"):
        return True
    if value.upper() in ("OFF", "FALSE", "NO", "N", "0", ""):
        return False
    raise ValueError(f"Not a recognized CMake boolean value: '{value}'")


def read_cmake_cache(build_dir: Path) -> dict[str, str]:
    """Returns the variables recorded in a build directory's CMakeCache.txt."""
    cache: dict[str, str] = {}
    for line in (build_dir / "CMakeCache.txt").read_text().splitlines():
        # Each entry has the form 'NAME:TYPE=VALUE'; comments begin with '#' or '//'
        m = re.fullmatch(r"([^#/:=][^:=]*):[A-Z]+=(.*)", line)
        if m:
            cache[m.group(1)] = m.group(2)
    return cache


def expected_license_files(platform: str, cache: dict[str, str]) -> set[str]:
    """
    Returns the third-party license texts that a package should contain, given the
    configuration it was built with: this must mirror the install rules for licenses/ in
    CMakeLists.txt, cmake/crashpad.cmake, and cmake/deps/libcurl.cmake.
    """
    names = {"date"}
    if platform == "windows":
        names.add("unicode-cldr")
    if cache.get("DD_CRASH_MODE") == "crashpad":
        names |= {"crashpad", "mini_chromium"}
        if platform == "linux":
            names.add("lss")
        if platform == "windows":
            names |= {"zlib", "getopt"}
    if (cache.get("DD_HTTP_CLIENT") == "libcurl"
            and not parse_cmake_bool(cache["DD_HTTP_USE_SYSTEM_LIBCURL"])):
        names.add("curl")
    return {f"{LICENSES_DIR}{name}.txt" for name in names}


def list_archive(archive_path: Path) -> list[str]:
    """Returns the paths of all files (not directories) in a .zip archive."""
    with zipfile.ZipFile(archive_path) as archive:
        return [info.filename for info in archive.infolist() if not info.is_dir()]


def main():
    parser = argparse.ArgumentParser(description=(
        "Verifies that the package produced by building the 'package' target is labeled "
        "with the expected linkage and contains the expected library, debug-info, and "
        "license files"
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

    # Check for required and forbidden files, reporting all problems at once. A package
    # must contain a license text for each third-party component built into it, and no
    # others, so that licenses/ accurately lists the third-party code in that package.
    license_files = expected_license_files(platform, read_cmake_cache(args.build_dir))
    print(f"Checking {archive_path.name} ({len(files)} files):")
    errors: list[str] = []
    required_files = COMMON_REQUIRED_FILES + REQUIRED_FILES[(platform, linkage)]
    for path in required_files + sorted(license_files):
        if path in files:
            print(f"- OK: {path}")
        else:
            errors.append(f"missing required file: {path}")
    for path in FORBIDDEN_FILES.get((platform, linkage), []):
        if path in files:
            errors.append(f"contains unexpected file: {path}")
    for path in sorted(files):
        if path.startswith(LICENSES_DIR) and path not in license_files:
            errors.append(f"contains unexpected license file: {path}")
    if errors:
        for error in errors:
            print(f"ERROR: {archive_path.name} {error}", file=sys.stderr)
        sys.exit(1)
    print("Package contents OK.")


if __name__ == "__main__":
    main()
