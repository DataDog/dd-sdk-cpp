# Unless explicitly stated otherwise all files in this repository are licensed under the
# Apache License Version 2.0.
#
# This product includes software developed at Datadog (https://www.datadoghq.com/).
# Copyright 2025-Present Datadog, Inc.
"""
bootstrap-crashpad
Usage: python3 tools/bootstrap-crashpad/main.py

This script is used to pull crashpad into the build, placing its source tree at
chromium/crashpad/crashpad and building it to chromium/crashpad/crashpad/out/Default,
or to the directory specified via --out-dir.

As part of the Chromium project, Crashpad uses `BUILD.gn` files to describe its build
configuration. Those files are fed into GN in order to generate Ninja build files. For
more information, see:

https://chromium.googlesource.com/crashpad/crashpad/+/HEAD/doc/developing.md

Commands:

- `main.py install`: clones depot_tools, runs `gclient sync` for crashpad source
- `main.py gn args --list`: Lists all input args for the generate gn build
- `main.py gn ls`: Lists all targets in the generated gn build
- `main.py gn desc //client:client --format=json`: Inspects gn build config for a target
- `main.py build`: runs install, builds with `gn gen` and `ninja build`, runs tests
    - add `--out-dir <path>` to specify a build directory
    - add `--no-install` to skip the install steps
    - add `--no-test` to skip running Crashpad's test suite
- `main.py test`: runs crashpad tests
- `main.py patch update <path>`: generate a fresh datadog.patch from a local crashpad
    clone; update the pinned revision in .gclient/.gclient_entries if the datadog branch
    has been rebased onto a new upstream commit
- `main.py dev init <path>`: bootstrap a local crashpad clone for development; creates/
    validates the datadog branch and optionally runs gclient sync
    - add `--no-sync` to skip gclient sync
- `main.py dev build <path>`: build the SDK using the local clone instead of the fetched
    chromium/crashpad/crashpad source tree

When building, relevant CMake options can be specified with `-c`, e.g.:

- `-c CMAKE_CXX_STANDARD=20 -c CMAKE_BUILD_TYPE=Release -c DD_COMPILE_OPTIONS="/W4|/Wx"`

If any CMake options are specified, the script will validate them and attempt to
configure the Crashpad build (via gn args) to produce libraries that are
binary-compatible with the encompassing CMake build. If any CMake options are specified,
the following values MUST be provided:

- CMAKE_CXX_STANDARD
- CMAKE_BUILD_TYPE
- (macOS only): CMAKE_OSX_DEPLOYMENT_TARGET
    - Note that this value must be set in CMakeLists.txt before ANY project()
        directive is processed
- (MSVC only): MSVC_RUNTIME_LIBRARY
"""
import os
import sys
import json
import platform
import pathlib
import subprocess
import argparse
import tempfile
from dataclasses import dataclass, field
from typing import Dict, List, Set

__repo_root__ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
__chromium_root__ = os.path.join(__repo_root__, 'chromium')
__depot_tools_root__ = os.path.join(__chromium_root__, 'depot_tools')
__crashpad_build_root__ = os.path.join(__chromium_root__, 'crashpad')
__crashpad_repo_root__ = os.path.join(__crashpad_build_root__, 'crashpad')
__crashpad_test_binaries__ = [
    'crashpad_client_test',
    'crashpad_handler_test',
    'crashpad_minidump_test',
    'crashpad_snapshot_test',
    'crashpad_test_test',
    'crashpad_util_test',
]
__default_crashpad_out_dir__ = os.path.join(__crashpad_repo_root__, 'out', 'Default')
__sdk_crashpad_out_dir__ = os.path.join(__repo_root__, 'build', '_deps', 'crashpad-build')

__patch_file__ = os.path.join(
    __repo_root__,
    'src', 'datadog', 'impl', 'crash_reporting', 'handlers', 'crashpad', 'datadog.patch'
)
__gclient_file__         = os.path.join(__crashpad_build_root__, '.gclient')
__gclient_entries_file__ = os.path.join(__crashpad_build_root__, '.gclient_entries')
__datadog_branch__       = 'datadog'


def _default_parallel_job_count() -> int:
    # For CMake builds in CI, we pass '--parallel ${NUM_PARALLEL_BUILD_JOBS}' to ensure
    # that builds don't overutilize resources, but this script is invoked from the
    # BUILD_COMMAND of a CMake ExternalProject target, which does not have access to the
    # CMAKE_BUILD_PARALLEL_LEVEL var, because the command is finalized at
    # configure-time. Therefore, in CI we can't explicitly control --parallel and must
    # instead check NUM_PARALLEL_BUILD_JOBS directly.
    num_parallel_build_jobs = os.environ.get('NUM_PARALLEL_BUILD_JOBS', '')
    if num_parallel_build_jobs:
        try:
            return int(num_parallel_build_jobs)
        except ValueError:
            pass

    # Inferring from CPU count works fine for local builds, but in CI this reflects the
    # CPU count of the host without respecting KUBERNETES_CPU_REQUEST etc.
    return os.cpu_count() - 1


def _gn_default_target_cpu():
    machine = platform.machine().lower()
    if machine in ("x86_64", "amd64"):
        return "x64"
    if machine in ("i386", "i686", "x86"):
        return "x86"
    if machine in ("aarch64", "arm64"):
        return "arm64"
    if machine.startswith("arm"):
        return "arm"
    raise RuntimeError(f"Unsupported architecture: {machine}")


@dataclass
class GnArgs:
    is_debug: bool = False
    target_cpu: str = _gn_default_target_cpu()
    mac_deployment_target: str = ''
    extra_cflags: Set[str] = field(default_factory=set)
    extra_cflags_c: Set[str] = field(default_factory=set)
    extra_cflags_cc: Set[str] = field(default_factory=set)
    extra_cflags_objc: Set[str] = field(default_factory=set)
    extra_cflags_objcc: Set[str] = field(default_factory=set)
    extra_ldflags: Set[str] = field(default_factory=set)
    extra_arflags: Set[str] = field(default_factory=set)

    def __str__(self) -> str:
        args = [
            f'is_debug={json.dumps(self.is_debug)}',
            f'target_cpu={json.dumps(self.target_cpu)}'
        ]
        if self.mac_deployment_target:
            args += [f'mac_deployment_target={json.dumps(self.mac_deployment_target)}']
        args += [
            f'extra_cflags={json.dumps(" ".join(sorted(self.extra_cflags)))}',
            f'extra_cflags_c={json.dumps(" ".join(sorted(self.extra_cflags_c)))}',
            f'extra_cflags_cc={json.dumps(" ".join(sorted(self.extra_cflags_cc)))}',
            f'extra_cflags_objc={json.dumps(" ".join(sorted(self.extra_cflags_objc)))}',
            f'extra_cflags_objcc={json.dumps(" ".join(sorted(self.extra_cflags_objcc)))}',
            f'extra_ldflags={json.dumps(" ".join(sorted(self.extra_ldflags)))}',
            f'extra_arflags={json.dumps(" ".join(sorted(self.extra_arflags)))}',
        ]
        return ' '.join(args)

    @classmethod
    def from_cmake(cls, vars: Dict[str, str]) -> 'GnArgs':
        args = GnArgs()

        # crashpad requires C++20; validate CMAKE_CXX_STANDARD for compatibility
        cmake_cxx_standard = vars.get('CMAKE_CXX_STANDARD', '')
        if cmake_cxx_standard and cmake_cxx_standard != '20':
            raise ValueError(f"Unsupported CMAKE_CXX_STANDARD: '{cmake_cxx_standard}'")

        # is_debug: Initialize from CMAKE_BUILD_TYPE (required)
        cmake_build_type = vars.get('CMAKE_BUILD_TYPE', '')
        if cmake_build_type == 'Debug':
            args.is_debug = True
        elif cmake_build_type in ('Release', 'RelWithDebInfo', 'MinSizeRel'):
            args.is_debug = False
        else:
            raise ValueError(f"Unsupported CMAKE_BUILD_TYPE: '{cmake_build_type}'")

        # target_cpu: Assume target arch is host arch (cross-compilation not supported)
        args.target_cpu = _gn_default_target_cpu()

        # mac_deployment_target: Initialize from CMAKE_OSX_DEPLOYMENT_TARGET
        if sys.platform == 'darwin':
            cmake_osx_deployment_target = vars.get('CMAKE_OSX_DEPLOYMENT_TARGET', '')
            if not cmake_osx_deployment_target:
                raise ValueError('CMAKE_OSX_DEPLOYMENT_TARGET must be specified for macOS builds')
            args.mac_deployment_target = cmake_osx_deployment_target

        # extra_cflags, extra_cflags_cc: Parse from DD_COMPILE_OPTIONS (pipe-separated).
        # crashpad's GN build uses its own compiler (from depot_tools), so we need to:
        # - drop any flags that aren't strictly necessary to ensure ABI compatibility
        # - preserve any flags that _do_ affect binary compatibility
        # - translate any foreign options (e.g. from MSVC) to natively-supported flags
        dd_compile_options_encoded = vars.get('DD_COMPILE_OPTIONS', '')
        dd_compile_options = {x.strip() for x in dd_compile_options_encoded.split('|') if x.strip()}
        for opt in dd_compile_options:
            # Strip any clang/GCC warning flags
            if opt.startswith('-W'):
                continue

            # Strip MSVC warning flags, including configuration for which includes are
            # treated as external and therefore exempt from strict warnings
            if opt.startswith('/W') or opt.startswith('/external:'):
                continue

            # The depot_tools compiler supports sanitizers, but it's not guaranteed to
            # use the same ABI version of ASan, TSan, etc.: strip these flags to ensure
            # that our crashpad build doesn't reference sanitizer symbols that are
            # incompatible with our SDK's build's sanitizer versions.
            if opt.startswith('-fsanitize') or opt.startswith('-fno-sanitize'):
                continue

            # All remaining flags are carried into the GN build directly: this is
            # brittle, but it ensures that we explicitly deal with all compiler flags
            args.extra_cflags.add(opt)
            args.extra_cflags_cc.add(opt)

        # extra_ldflags: Parse from DD_LINK_OPTIONS (pipe-separated)
        dd_link_options_encoded = vars.get('DD_LINK_OPTIONS', '')
        dd_link_options = {x.strip() for x in dd_link_options_encoded.split('|') if x.strip()}
        for opt in dd_link_options:
            # Strip sanitizer flags for the same reasons noted above
            if opt.startswith('-fsanitize') or opt.startswith('-fno-sanitize'):
                continue
            args.extra_ldflags.add(opt)

        # On Windows, require MSVC_RUNTIME_LIBRARY and ensure that we're building
        # crashpad with the appropriate CRT binary
        if sys.platform == 'win32':
            # Map CMake options to the corresponding switches provided to cl.exe
            crt_flags = {
                'MultiThreaded': '/MT',
                'MultiThreadedDLL': '/MD',
                'MultiThreadedDebug': '/MTd',
                'MultiThreadedDebugDLL': '/MDd',
            }

            # Identify the CRT flag that we should be passing to the compiler
            msvc_runtime_library = vars.get('MSVC_RUNTIME_LIBRARY', '')
            if not msvc_runtime_library:
                raise ValueError('MSVC_RUNTIME_LIBRARY must be specified for Windows builds')
            crt_flag = crt_flags.get(msvc_runtime_library)
            if not crt_flag:
                raise ValueError(f"Unsupported MSVC_RUNTIME_LIBRARY: '{msvc_runtime_library}'")

            # Discard any CRT flags that were previously set via DD_COMPILE_OPTIONS, to
            # ensure that we provide a single, unambiguous value
            for flag_to_discard in crt_flags.values():
                args.extra_cflags.discard(flag_to_discard)
                args.extra_cflags_cc.discard(flag_to_discard)
            
            # Supply the appropriate CRT flag (/MT, /MD, etc.) to cl.exe to ensure that
            # crashpad binaries will be compatible with artifacts from the CMake build
            args.extra_cflags.add(crt_flag)
            args.extra_cflags_cc.add(crt_flag)

        # The Crashpad build will resolve the appropriate toolchain based on platform
        # (e.g. "gcc_like_toolchain" on macOS/Linux; "msvc_toolchain_x64" on Windows),
        # following the rules defined for that toolchain in:
        #
        # - chromium/crashpad/crashpad/third_party/mini_chromium/build/config/BUILD.gn
        #
        # It's not strictly guaranteed that this will end up being the same compiler and
        # linker used by the CMake build. For better compatibility guarantees, we may
        # want to add some additional validation/configuration here.

        return args


def _depot_tools_env() -> Dict[str, str]:
    env = os.environ.copy()
    env['PATH'] = __depot_tools_root__ + os.pathsep + env['PATH']
    return env


def _run_depot_tool(args: List[str], cwd: str) -> int:
    if sys.platform == 'win32':
        args[0] = os.path.join(__depot_tools_root__, args[0] + '.bat')
    return subprocess.check_call(args, env=_depot_tools_env(), cwd=cwd)


def read_pinned_revision() -> str:
    """Read the crashpad commit hash pinned in chromium/crashpad/.gclient_entries."""
    if not os.path.isfile(__gclient_entries_file__):
        raise RuntimeError(
            f'.gclient_entries not found at {__gclient_entries_file__}. '
            'Run `main.py install` first.'
        )
    with open(__gclient_entries_file__, 'r') as f:
        content = f.read()
    namespace: Dict[str, object] = {}
    exec(content, namespace)  # noqa: S102 — trusted content from our own repo
    entries = namespace.get('entries')
    if not isinstance(entries, dict):
        raise RuntimeError(
            f'Could not parse entries dict from {__gclient_entries_file__}'
        )
    crashpad_url = entries.get('crashpad', '')
    if '@' not in crashpad_url:
        raise RuntimeError(
            f'Unexpected crashpad entry in .gclient_entries (no @HASH): {crashpad_url!r}'
        )
    return crashpad_url.rsplit('@', 1)[1]


def write_pinned_revision(new_hash: str) -> None:
    """Atomically update the crashpad pin in both .gclient and .gclient_entries."""
    old_hash = read_pinned_revision()
    for path in (__gclient_file__, __gclient_entries_file__):
        with open(path, 'r') as f:
            content = f.read()
        count = content.count(old_hash)
        if count != 1:
            raise RuntimeError(
                f'Expected exactly one occurrence of {old_hash!r} in {path}, '
                f'found {count}'
            )
        new_content = content.replace(old_hash, new_hash, 1)
        # Write atomically: temp file alongside, then rename
        dir_ = os.path.dirname(path)
        fd, tmp_path = tempfile.mkstemp(dir=dir_)
        try:
            with os.fdopen(fd, 'w') as f:
                f.write(new_content)
            os.replace(tmp_path, path)
        except Exception:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass
            raise
    print(f'Updated pin: {old_hash} → {new_hash}')


def apply_patch(repo_root: str) -> None:
    """Idempotently apply datadog.patch to the git repo at repo_root."""
    # If the patch file is absent or empty there is nothing to do
    if not os.path.isfile(__patch_file__) or os.path.getsize(__patch_file__) == 0:
        print('No patch to apply.')
        return

    # Check that the repo HEAD matches the pinned revision before attempting to apply.
    # A mismatch means the source tree is out of date and the patch may not apply cleanly.
    pinned = read_pinned_revision()
    result = subprocess.run(
        ['git', 'rev-parse', 'HEAD'],
        cwd=repo_root, capture_output=True, text=True
    )
    if result.returncode == 0:
        actual = result.stdout.strip()
        if actual != pinned:
            raise RuntimeError(
                f'Source tree HEAD is {actual!r} but datadog.patch was generated '
                f'against {pinned!r}. Run `gclient sync` to update the source tree '
                f'before applying the patch.'
            )

    # Try to apply the patch cleanly. --ignore-whitespace ensures that line-ending
    # differences (e.g. LF patch applied to a CRLF checkout on Windows) don't cause
    # spurious context mismatches.
    check_result = subprocess.run(
        ['git', 'apply', '--ignore-whitespace', '--check', __patch_file__],
        cwd=repo_root, capture_output=True, text=True
    )
    if check_result.returncode == 0:
        # Patch applies cleanly — go ahead and apply it
        subprocess.check_call(['git', 'apply', '--ignore-whitespace', __patch_file__], cwd=repo_root)
        print('Patch applied successfully.')
        return

    # Forward apply failed. Check whether the patch is already applied by trying the
    # reverse: this disambiguates "already applied" from other failures (corrupt patch,
    # wrong base, etc.)
    reverse_result = subprocess.run(
        ['git', 'apply', '--ignore-whitespace', '--check', '--reverse', __patch_file__],
        cwd=repo_root, capture_output=True, text=True
    )
    if reverse_result.returncode == 0:
        print('Patch already applied — skipping.')
        return

    # Neither forward nor reverse apply succeeded: the repo is in an unexpected state.
    status_result = subprocess.run(
        ['git', 'status'], cwd=repo_root, capture_output=True, text=True
    )
    diff_result = subprocess.run(
        ['git', 'diff', '--stat', 'HEAD'], cwd=repo_root, capture_output=True, text=True
    )
    raise RuntimeError(
        f'datadog.patch could not be applied to the repo at {repo_root!r} and does not '
        f'appear to be already applied. The repo may be in an unexpected state.\n\n'
        f'git apply --check stderr:\n{check_result.stderr}\n'
        f'git status:\n{status_result.stdout}\n'
        f'git diff --stat HEAD:\n{diff_result.stdout}'
    )


def clone_depot_tools():
    # If needed, clone into chromium/depot_tools so we can use 'fetch' and related tools
    # to pull down Chromium-project source repos
    os.makedirs(__chromium_root__, exist_ok=True)
    if not os.path.isdir(__depot_tools_root__):
        print('Cloning depot_tools...')
        subprocess.check_call(['git', 'clone', 'https://chromium.googlesource.com/chromium/tools/depot_tools.git'], cwd=__chromium_root__)
        assert os.path.isdir(__depot_tools_root__)
    print(f'depot_tools present at: {__depot_tools_root__}')


def fetch_crashpad():
    # If crashpad is not present, use chromium/depot_tools/fetch to clone it: note that
    # chromium/crashpad is the root directory for the build (containing metadata files
    # like `.gclient`) while chromium/crashpad/crashpad is the root of the repo
    if not os.path.isfile(os.path.join(__crashpad_build_root__, '.gclient')):
        print('Fetching crashpad...')
        os.makedirs(__crashpad_build_root__, exist_ok=True)
        _run_depot_tool(['fetch', 'crashpad'], __crashpad_build_root__)
        assert os.path.isdir(__crashpad_repo_root__)
    else:
        print('Syncing crashpad...')
        _run_depot_tool(['gclient', 'sync'], __crashpad_build_root__)
    print(f'Crashpad is present at: {__crashpad_repo_root__}')


def build_crashpad(out_dir: str, gn_args: GnArgs, num_parallel_jobs: int, source_root: str = None):
    if source_root is None:
        source_root = __crashpad_repo_root__

    # depot_tools and the crashpad source must be present
    assert os.path.isdir(__depot_tools_root__)
    assert os.path.isdir(source_root)

    # Use gn gen to create Ninja build files
    print('Invoking GN to generate Ninja build files for crashpad...')
    _run_depot_tool(['gn', 'gen', out_dir, f'--args={str(gn_args)}'], source_root)
    print('Invoking Ninja to build crashpad...')
    _run_depot_tool(['ninja', '-C', out_dir, '-j', str(num_parallel_jobs)], source_root)


def run_crashpad_tests(out_dir: str, source_root: str = None):
    if source_root is None:
        source_root = __crashpad_repo_root__
    env = os.environ.copy()

    # Some Crashpad tests need to resolve files from crashpad/test/ (within the source
    # tree): by default, these tests assume they've been built to `out/{Debug,Release}`
    # and search in `../..` to resolve the root crashpad source directory. Since we may
    # build Crashpad in a different directory altogether (out_dir), we need to
    # explicitly point the tests to the crashpad source dir so they can locate the
    # necessary test data files.
    env['CRASHPAD_TEST_DATA_ROOT'] = source_root

    # We run the Crashpad test suite in CI as a best-effort to ensure that we've
    # produced a working build, but we need to skip a handful of problematic tests
    excluded_tests: List[str] = []
    if sys.platform == 'linux':
        # On Linux/Clang, when built in Debug mode, a subset of 16
        # StartHandlerForSelfTest.StartHandlerInChild tests fail: these are cases where
        # `crash_non_main_thread = true` and `crash_type = kInfiniteRecursion`. (These
        # tests pass in Release builds, but they behave differently in Debug builds.)
        excluded_tests.append('*/StartHandlerForSelfTest.StartHandlerInChild/*')
    if sys.platform == 'win32':
        # Windows Docker containers don't have timezone info
        excluded_tests.append('SystemSnapshotWinTest.TimeZone')

        # These tests pass, but as a side effect they print output containing the
        # substring 'error:', which MSBuild interprets as build failure
        excluded_tests.append('WinMultiprocessChildFails.*')

        # Cross-process thread introspection via NtSuspendProcess is unreliable
        # inside a Windows Docker container and causes this test to fail
        excluded_tests.append('ProcessReaderWin.ChildThreadSuspendCounts')
    if excluded_tests:
        env['GTEST_FILTER'] = '-' + ':'.join(excluded_tests)

    for binary_name in __crashpad_test_binaries__:
        binary_path = os.path.join(out_dir, binary_name)
        if sys.platform == 'win32':
            binary_path += '.exe'
        assert os.path.isfile(binary_path)
        print(f'Running {binary_name}...')
        subprocess.check_call([binary_path], env=env)
    print('All crashpad tests passed.')


def install_main(args: argparse.Namespace):
    clone_depot_tools()
    fetch_crashpad()


def gn_main(args: argparse.Namespace):
    _run_depot_tool(['gn', args.command, args.out_dir] + args.command_args, __crashpad_repo_root__)


def build_main(args: argparse.Namespace):
    # Seamlessly install tools and sync the crashpad repo unless instructed not to
    if not args.no_install:
        clone_depot_tools()
        fetch_crashpad()

    # Parse '-c FOO=ON -c BAR=bar' into {'FOO': 'ON', 'BAR': 'bar'}
    cmake_vars: Dict[str, str] = {}
    for s in args.cmake_vars:
        if '=' not in s:
            raise ValueError(f"Unexpected format for CMake var: '{s}'")
        k, v = s.split('=', 1)
        cmake_vars[k] = v

    # If we've been provided with any CMake args, resolve the corresponding set of gn
    # args to ensure that our crashpad build is binary-compatible with our CMake build
    gn_args = GnArgs()
    if cmake_vars:
        gn_args = GnArgs.from_cmake(cmake_vars)

    # Apply our patch to the checked-out crashpad source (idempotent)
    apply_patch(__crashpad_repo_root__)

    # Use gn and ninja to produce a build of crashpad
    build_crashpad(args.out_dir, gn_args, args.parallel)

    # Run crashpad tests to validate the build, unless instructed not to
    if not args.no_test:
        run_crashpad_tests(args.out_dir)


def test_main(args: argparse.Namespace):
    local_clone = os.path.abspath(args.local_clone) if args.local_clone else None
    if args.out_dir is not None:
        out_dir = args.out_dir
    elif local_clone is not None:
        out_dir = __sdk_crashpad_out_dir__
    else:
        out_dir = __default_crashpad_out_dir__
    run_crashpad_tests(out_dir, source_root=local_clone)


def patch_update_main(args: argparse.Namespace):
    local_clone = os.path.abspath(args.local_clone)

    base_hash = read_pinned_revision()
    print(f'Current pinned revision: {base_hash}')

    # Verify the local clone is a git repository
    if subprocess.run(['git', 'rev-parse', '--git-dir'], cwd=local_clone,
                      capture_output=True).returncode != 0:
        raise RuntimeError(f'{local_clone!r} is not a git repository')

    # Verify the datadog branch exists
    if subprocess.run(['git', 'rev-parse', '--verify', __datadog_branch__],
                      cwd=local_clone, capture_output=True).returncode != 0:
        raise RuntimeError(
            f'Branch {__datadog_branch__!r} does not exist in {local_clone!r}. '
            'Create it with `dev init` first.'
        )

    # Find the fork point: the upstream commit that the datadog branch is directly based on.
    # We use merge-base(datadog, origin/main) rather than merge-base(datadog, base_hash),
    # because the latter always returns base_hash (it's always an ancestor of datadog), and
    # therefore can't detect that the developer has rebased onto a newer upstream commit.
    # origin/main must be up to date — remind the user to `git fetch` if it's stale.
    fork_point_result = subprocess.run(
        ['git', 'merge-base', __datadog_branch__, 'origin/main'],
        cwd=local_clone, capture_output=True, text=True
    )
    if fork_point_result.returncode != 0:
        raise RuntimeError(
            f'Could not determine fork point of {__datadog_branch__!r} and origin/main '
            f'in {local_clone!r}. Ensure origin/main is up to date (run `git fetch origin`).'
        )
    fork_point = fork_point_result.stdout.strip()

    # Sanity check: the fork point must be a descendant-or-equal of the current pin.
    # If it isn't, the datadog branch has been rebased onto something that doesn't descend
    # from our known-good base — most likely origin/main is stale or the clone is wrong.
    if subprocess.run(
        ['git', 'merge-base', '--is-ancestor', base_hash, fork_point],
        cwd=local_clone, capture_output=True
    ).returncode != 0:
        raise RuntimeError(
            f'The fork point of {__datadog_branch__!r} and origin/main ({fork_point}) is not '
            f'a descendant of the current pin ({base_hash}).\n'
            f'Ensure origin/main is up to date (`git fetch origin`) and that the '
            f'{__datadog_branch__!r} branch is rebased onto a commit that descends from the pin.'
        )

    # Produce the diff and write datadog.patch
    diff_result = subprocess.run(
        ['git', 'diff', fork_point, __datadog_branch__],
        cwd=local_clone, capture_output=True, text=True, check=True
    )
    diff_content = diff_result.stdout
    if not diff_content:
        print('Warning: no differences between fork point and datadog branch — writing empty patch.')
    with open(__patch_file__, 'w') as f:
        f.write(diff_content)
    print(f'Wrote {__patch_file__}')

    # The new pin is the fork point: when the developer hasn't rebased, this equals
    # base_hash and the pin is unchanged. When they've rebased onto a newer upstream
    # commit, this is that newer commit and the pin advances accordingly.
    new_base_hash = fork_point

    # Update pin files if the base has advanced
    if new_base_hash != base_hash:
        write_pinned_revision(new_base_hash)

    line_count = len(diff_content.splitlines()) if diff_content else 0
    files_changed = len([l for l in diff_content.splitlines() if l.startswith('diff --git')])
    print(f'Patch summary: {line_count} lines, {files_changed} file(s) changed')
    print(f'Pin: {new_base_hash}')

    # Remind the user what to commit
    print()
    print('Remember to commit the following files together:')
    print(f'  {__patch_file__}')
    if new_base_hash != base_hash:
        print(f'  {__gclient_file__}')
        print(f'  {__gclient_entries_file__}')


def dev_init_main(args: argparse.Namespace):
    local_clone = os.path.abspath(args.local_clone)

    # Pre-validate local_clone before doing any network work
    if not os.path.isdir(local_clone) or not os.listdir(local_clone):
        raise RuntimeError(
            f'{local_clone!r} does not exist or is empty.\n'
            'Please clone chromium/crashpad there first:\n'
            f'  git clone git@github.com:chromium/crashpad.git {local_clone}'
        )
    remote_result = subprocess.run(
        ['git', 'remote', 'get-url', 'origin'],
        cwd=local_clone, capture_output=True, text=True
    )
    _valid_remotes = ('chromium/crashpad', 'chromium.googlesource.com/crashpad/crashpad')
    if remote_result.returncode != 0 or not any(r in remote_result.stdout for r in _valid_remotes):
        raise RuntimeError(
            f'{local_clone!r} does not appear to be a clone of chromium/crashpad.\n'
            f'  origin URL: {remote_result.stdout.strip()!r}'
        )

    # Ensure depot_tools is available
    clone_depot_tools()

    base_hash = read_pinned_revision()
    print(f'Pinned revision: {base_hash}')

    # Ensure base_hash is present in the clone; fetch if not
    def _has_commit(hash_: str) -> bool:
        return subprocess.run(
            ['git', 'cat-file', '-e', f'{hash_}^{{commit}}'],
            cwd=local_clone, capture_output=True
        ).returncode == 0

    if not _has_commit(base_hash):
        print(f'{base_hash} not found locally — fetching origin...')
        subprocess.check_call(['git', 'fetch', 'origin'], cwd=local_clone)
        if not _has_commit(base_hash):
            raise RuntimeError(
                f'Commit {base_hash} is not present in {local_clone!r} even after '
                f'fetching. Check that this clone tracks the correct remote.'
            )

    # Create or validate the datadog branch
    branch_exists = subprocess.run(
        ['git', 'rev-parse', '--verify', __datadog_branch__],
        cwd=local_clone, capture_output=True
    ).returncode == 0

    if not branch_exists:
        print(f'Creating branch {__datadog_branch__!r} at {base_hash}...')
        subprocess.check_call(
            ['git', 'checkout', '-b', __datadog_branch__, base_hash],
            cwd=local_clone
        )
    else:
        branch_tip = subprocess.run(
            ['git', 'rev-parse', __datadog_branch__],
            cwd=local_clone, capture_output=True, text=True, check=True
        ).stdout.strip()
        if branch_tip == base_hash:
            print(f'Branch {__datadog_branch__!r} already at correct base — checking out.')
            subprocess.check_call(['git', 'checkout', __datadog_branch__], cwd=local_clone)
        else:
            raise RuntimeError(
                f'Branch {__datadog_branch__!r} exists but points to {branch_tip!r}, '
                f'not the pinned revision {base_hash!r}.\n'
                f'Either delete the branch (`git branch -D {__datadog_branch__}`) or '
                f'rebase it onto {base_hash}, then re-run dev init.'
            )

    # Optionally run gclient sync to populate third_party/ and buildtools/
    if not args.no_sync:
        parent_dir = os.path.dirname(local_clone)
        gclient_path = os.path.join(parent_dir, '.gclient')
        abs_clone = os.path.abspath(local_clone)
        clone_uri = pathlib.Path(abs_clone).as_uri()
        gclient_content = (
            'solutions = [\n'
            '  {\n'
            f'    "name": "{os.path.basename(local_clone)}",\n'
            f'    "url": "{clone_uri}@{base_hash}",\n'
            '    "managed": False,\n'
            '  },\n'
            ']\n'
        )
        if os.path.exists(gclient_path):
            with open(gclient_path) as f:
                existing = f.read()
            if existing == gclient_content:
                print(f'{gclient_path} already up to date.')
            else:
                raise RuntimeError(
                    f'{gclient_path} already exists with different content.\n'
                    f'Remove it or move it aside, then re-run dev init.\n'
                    f'Expected:\n{gclient_content}'
                )
        else:
            with open(gclient_path, 'w') as f:
                f.write(gclient_content)
            print(f'Wrote {gclient_path}')
        print('Running gclient sync (this may take a few minutes on first run)...')
        _run_depot_tool(['gclient', 'sync'], parent_dir)

    # Apply the patch if non-empty
    apply_patch(local_clone)

    print()
    print('dev init complete.')
    print(f'  Local clone : {local_clone}')
    print(f'  Branch      : {__datadog_branch__}')
    print(f'  Base commit : {base_hash}')


def dev_build_main(args: argparse.Namespace):
    local_clone = os.path.abspath(args.local_clone)

    # depot_tools is required for gn/ninja regardless of source origin
    clone_depot_tools()

    # Parse '-c FOO=ON -c BAR=bar' into {'FOO': 'ON', 'BAR': 'bar'}
    cmake_vars: Dict[str, str] = {}
    for s in args.cmake_vars:
        if '=' not in s:
            raise ValueError(f"Unexpected format for CMake var: '{s}'")
        k, v = s.split('=', 1)
        cmake_vars[k] = v

    gn_args = GnArgs()
    if cmake_vars:
        gn_args = GnArgs.from_cmake(cmake_vars)

    # Build from the local clone; the datadog branch already carries the changes,
    # so there is no need to apply the patch here
    build_crashpad(args.out_dir, gn_args, args.parallel, source_root=local_clone)

    if not args.no_test:
        run_crashpad_tests(args.out_dir, source_root=local_clone)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest='command', required=True)

    install_parser = subparsers.add_parser('install')
    install_parser.set_defaults(func=install_main)

    gn_parser = subparsers.add_parser('gn')
    gn_parser.set_defaults(func=gn_main)
    gn_parser.add_argument('command', default='args', help='gn subcommand to invoke, e.g. args, ls, desc')
    gn_parser.add_argument('--out-dir', '-o', default=__default_crashpad_out_dir__, help='Output directory whose gn configuration should be inspected')
    gn_parser.add_argument('command_args', nargs=argparse.REMAINDER, help='Additional args to pass to the gn subcommand')

    build_parser = subparsers.add_parser('build')
    build_parser.add_argument('--out-dir', '-o', default=__default_crashpad_out_dir__, help='Output directory for crashpad build files and artifacts')
    build_parser.add_argument('--no-install', '-I', action='store_true', help='If set, skip install/sync of crashpad and depot_tools')
    build_parser.add_argument('--no-test', '-T', action='store_true', help='If set, skip running crashpad tests post-build')
    build_parser.add_argument('--cmake-var', '-c', action='append', dest='cmake_vars', default=[], help='CMake var in KEY=VALUE format; used to configure GN build')
    build_parser.add_argument('--parallel', '-j', type=int, default=_default_parallel_job_count(), help='Number of parallel build jobs to use when invoking ninja')
    build_parser.set_defaults(func=build_main)

    test_parser = subparsers.add_parser('test')
    test_parser.add_argument('local_clone', nargs='?', default=None, help='Path to a local crashpad clone set up via dev init; when supplied, defaults --out-dir to the SDK build output')
    test_parser.add_argument('--out-dir', '-o', default=None, help='Output directory where crashpad test binaries are located')
    test_parser.set_defaults(func=test_main)

    # patch <subcommand>
    patch_parser = subparsers.add_parser('patch')
    patch_subparsers = patch_parser.add_subparsers(dest='patch_command', required=True)

    patch_update_parser = patch_subparsers.add_parser('update')
    patch_update_parser.add_argument('local_clone', help='Path to a local chromium/crashpad git clone')
    patch_update_parser.set_defaults(func=patch_update_main)

    # dev <subcommand>
    dev_parser = subparsers.add_parser('dev')
    dev_subparsers = dev_parser.add_subparsers(dest='dev_command', required=True)

    dev_init_parser = dev_subparsers.add_parser('init')
    dev_init_parser.add_argument('local_clone', help='Path to an existing local chromium/crashpad git clone')
    dev_init_parser.add_argument('--no-sync', action='store_true', help='Skip gclient sync')
    dev_init_parser.set_defaults(func=dev_init_main)

    dev_build_parser = dev_subparsers.add_parser('build')
    dev_build_parser.add_argument('local_clone', help='Path to a local chromium/crashpad git clone set up via dev init')
    dev_build_parser.add_argument('--out-dir', '-o', default=__default_crashpad_out_dir__, help='Output directory for crashpad build files and artifacts')
    dev_build_parser.add_argument('--no-test', '-T', action='store_true', help='If set, skip running crashpad tests post-build')
    dev_build_parser.add_argument('--cmake-var', '-c', action='append', dest='cmake_vars', default=[], help='CMake var in KEY=VALUE format; used to configure GN build')
    dev_build_parser.add_argument('--parallel', '-j', type=int, default=_default_parallel_job_count(), help='Number of parallel build jobs to use when invoking ninja')
    dev_build_parser.set_defaults(func=dev_build_main)

    args = parser.parse_args()
    args.func(args)
