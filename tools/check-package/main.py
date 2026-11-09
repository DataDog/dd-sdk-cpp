#!/usr/bin/env python3
# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
import os
import re
import sys
import shutil
import zipfile
import argparse
import tempfile
import subprocess
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

# CMake project that links a packaged static library into a shared library, so that the
# result can be inspected for unresolved symbols and required symbol versions
LINK_TEST_DIR = Path(__file__).parent / "link-test"

# libstdc++ defines its C++11-conforming std::string, std::list, etc. in the inline
# namespace std::__cxx11, which mangles as 'St7__cxx11' or 'NSt7__cxx11'. libc++ uses
# the inline namespace std::__1 instead, which mangles as 'St3__1' or 'NSt3__1'.
LIBSTDCXX_NAMESPACE_MANGLED = "St7__cxx11"
LIBCXX_NAMESPACE_MANGLED = "St3__1"

# Symbol version tags that only libstdc++ defines
LIBSTDCXX_VERSION_TAG_PREFIXES = ("GLIBCXX_", "CXXABI_")


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
    CMakeLists.txt, cmake/crashpad.cmake, cmake/deps/libcurl.cmake, and
    cmake/deps/dd-win-prof.cmake.
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
    if cache.get("DD_PROFILER_IMPL") == "dd-win-prof":
        names |= {"dd-win-prof", "libdatadog", "spdlog", "fmt"}
    if (cache.get("DD_HTTP_CLIENT") == "libcurl"
            and not parse_cmake_bool(cache["DD_HTTP_USE_SYSTEM_LIBCURL"])):
        names.add("curl")
    return {f"{LICENSES_DIR}{name}.txt" for name in names}


def parse_version(value: str) -> tuple[int, ...]:
    """Parses a dotted version number such as '2.28' into a comparable tuple."""
    if not re.fullmatch(r"\d+(\.\d+)*", value):
        raise argparse.ArgumentTypeError(f"Not a version number: '{value}'")
    return tuple(int(part) for part in value.split("."))


def run(args: list[str], **kwargs) -> str:
    """Runs a command, exiting with its output if it fails; returns its stdout."""
    result = subprocess.run(args, capture_output=True, text=True, **kwargs)
    if result.returncode != 0:
        sys.exit(f"ERROR: Command failed ({result.returncode}): {' '.join(args)}\n"
                 f"{result.stdout}{result.stderr}")
    return result.stdout


def require_tool(cache: dict[str, str], name: str) -> str:
    """Returns the path to a binutils tool recorded in CMakeCache.txt, e.g. CMAKE_NM."""
    path = cache.get(name, "")
    if not path or path.endswith("-NOTFOUND") or not Path(path).is_file():
        sys.exit(f"ERROR: {name} is not set to a valid path in CMakeCache.txt")
    return path


def check_libstdcxx_symbols(nm: str, static_library: Path) -> list[str]:
    """
    Verifies that a static library was compiled against libc++ rather than libstdc++,
    by examining the mangled names of the symbols it defines and references. Returns a
    list of errors.
    """
    symbols = run([nm, "--format=just-symbols", str(static_library)]).split()
    libstdcxx_symbols = sorted({s for s in symbols if LIBSTDCXX_NAMESPACE_MANGLED in s})
    errors: list[str] = []
    if libstdcxx_symbols:
        errors.append(f"{static_library.name} has {len(libstdcxx_symbols)} symbols in "
                      f"libstdc++'s std::__cxx11 namespace, e.g. {libstdcxx_symbols[0]}")
    if not any(LIBCXX_NAMESPACE_MANGLED in s for s in symbols):
        errors.append(f"{static_library.name} has no symbols in libc++'s std::__1 "
                      f"namespace; it was not compiled against libc++")
    return errors


def link_shared_library(cache: dict[str, str], package_dir: Path, work_dir: Path) -> Path:
    """
    Links the static library in an extracted package into a shared library, by building
    the CMake project in LINK_TEST_DIR with the same toolchain file as the package build.
    The link fails (and we exit) if the library has unresolved symbols. On success,
    returns the path to the shared library.
    """
    build_dir = work_dir / "link-test-build"
    configure_args = [
        "cmake", "-S", str(LINK_TEST_DIR), "-B", str(build_dir),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DDatadog_DIR={package_dir / 'lib' / 'cmake' / 'Datadog'}",
    ]
    toolchain_file = (cache.get("CMAKE_TOOLCHAIN_FILE")
                      or os.environ.get("CMAKE_TOOLCHAIN_FILE"))
    if toolchain_file:
        configure_args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}")
    run(configure_args)
    run(["cmake", "--build", str(build_dir)])
    return build_dir / "liblink_test.so"


def check_symbol_versions(readelf: str, shared_library: Path,
                          max_glibc_version: tuple[int, ...] | None,
                          forbid_libstdcxx: bool) -> list[str]:
    """
    Verifies the symbol versions that a shared library requires from the libraries it
    links dynamically: no glibc version newer than `max_glibc_version`, if given, and no
    libstdc++ versions, if `forbid_libstdcxx`. Returns a list of errors.
    """
    # The version-needs section lists each required version as a line such as
    # '  0x0010:   Name: GLIBC_2.2.5  Flags: none  Version: 3'
    output = run([readelf, "--version-info", "--wide", str(shared_library)])
    required = sorted(set(re.findall(r"Name: (\S+)\s+Flags:", output)))
    errors: list[str] = []
    for tag in required:
        m = re.fullmatch(r"GLIBC_(\d+(?:\.\d+)*)", tag)
        if m and max_glibc_version and parse_version(m.group(1)) > max_glibc_version:
            errors.append(f"requires {tag}, newer than the maximum of GLIBC_"
                          f"{'.'.join(str(n) for n in max_glibc_version)}")
        if forbid_libstdcxx and tag.startswith(LIBSTDCXX_VERSION_TAG_PREFIXES):
            errors.append(f"requires libstdc++ symbol version {tag}")
    glibc_tags = [tag for tag in required if tag.startswith("GLIBC_2")]
    print(f"- Linked library requires: {', '.join(glibc_tags) or 'no glibc versions'}")
    return errors


def check_static_library_linkage(cache: dict[str, str], archive_path: Path, prefix: str,
                                 max_glibc_version: tuple[int, ...] | None,
                                 forbid_libstdcxx: bool) -> list[str]:
    """
    Extracts a Linux static-library package and verifies which C++ standard library and
    glibc symbol versions its library depends on. Returns a list of errors.
    """
    nm = require_tool(cache, "CMAKE_NM")
    readelf = require_tool(cache, "CMAKE_READELF")
    work_dir = Path(tempfile.mkdtemp(prefix="check-package-"))
    try:
        with zipfile.ZipFile(archive_path) as archive:
            archive.extractall(work_dir)
        package_dir = work_dir / prefix.rstrip("/")
        static_library = package_dir / "lib" / "libddsdkcpp.a"

        errors: list[str] = []
        if forbid_libstdcxx:
            print(f"Checking {static_library.name} for libstdc++ symbols:")
            errors += check_libstdcxx_symbols(nm, static_library)

        # Undefined symbols in a static library carry no symbol versions: versions are
        # bound only when a binary links against the shared libraries that define them,
        # so we link a shared library from the archive and inspect that
        print(f"Linking {static_library.name} into a shared library:")
        shared_library = link_shared_library(cache, package_dir, work_dir)
        errors += check_symbol_versions(readelf, shared_library, max_glibc_version,
                                        forbid_libstdcxx)
        return errors
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)


def list_archive(archive_path: Path) -> list[str]:
    """Returns the paths of all files (not directories) in a .zip archive."""
    with zipfile.ZipFile(archive_path) as archive:
        return [info.filename for info in archive.infolist() if not info.is_dir()]


def main():
    parser = argparse.ArgumentParser(description=(
        "Verifies that the package produced by building the 'package' target is labeled "
        "with the expected linkage and contains the expected library, debug-info, and "
        "license files, and optionally checks a Linux static library's C++ standard "
        "library and glibc dependencies"
    ))
    parser.add_argument("build_dir", type=Path,
                        help="Build directory containing artifact_name.txt and the archive")
    parser.add_argument("--build-shared-libs", required=True,
                        help="Value of BUILD_SHARED_LIBS used to configure the build")
    parser.add_argument("--max-glibc-version", type=parse_version, default=None,
                        help=("For a Linux static library: fail if linking it requires a "
                              "glibc symbol version newer than this, e.g. 2.28"))
    parser.add_argument("--forbid-libstdc++", dest="forbid_libstdcxx",
                        action="store_true",
                        help=("For a Linux static library: fail unless it was compiled "
                              "against libc++, with no references to libstdc++"))
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

    # The linkage checks link the library with the package build's toolchain, which only
    # applies to Linux static libraries
    check_linkage = args.max_glibc_version is not None or args.forbid_libstdcxx
    if check_linkage and (platform, linkage) != ("linux", "static"):
        sys.exit("ERROR: --max-glibc-version and --forbid-libstdc++ apply only to Linux "
                 f"static-library packages, not '{artifact_name}'")

    # Check for required and forbidden files, reporting all problems at once. A package
    # must contain a license text for each third-party component built into it, and no
    # others, so that licenses/ accurately lists the third-party code in that package.
    cache = read_cmake_cache(args.build_dir)
    license_files = expected_license_files(platform, cache)
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

    # Verify the library's C++ standard library and glibc dependencies, if requested
    if check_linkage:
        errors = check_static_library_linkage(cache, archive_path, prefix,
                                              args.max_glibc_version,
                                              args.forbid_libstdcxx)
        if errors:
            for error in errors:
                print(f"ERROR: {archive_path.name} {error}", file=sys.stderr)
            sys.exit(1)
        print("Library linkage OK.")


if __name__ == "__main__":
    main()
