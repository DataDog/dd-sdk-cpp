# dd-win-prof integration into dd-sdk-cpp

## Context
Integrate dd-win-prof as an opt-in shared library built through our CMake. Default build and non-Windows builds must not touch it. Longer-term, dd-sdk-cpp should own pprof serialization + upload so libdatadog can be dropped. Three tracks, in order: (A) upstream dd-win-prof PR to stop forcing gtest/test targets on consumers, (B) the SDK CMake integration, (C) separate libdatadog-removal effort.

Track C details (libdatadog usage inventory, per-phase breakdown, risks) live in `.plans/libdatadog-removal.md`.

## Decisions (from user)
- Pin dd-win-prof to a commit SHA. Flag `DD_ENABLE_PROFILING` (OFF). Min CMake -> 3.28.
- Non-Windows: flag ignored (status message). Windows non-x64 (x86/ARM64): configure error.
- Stay at C++17 (dd-win-prof sets C++20 in its own dir; C ABI boundary makes mixing safe; `#include <cstdint>` before `dd-win-prof.h`).
- Link PRIVATE (SDK owns the profiler lifecycle; apps use SDK API, not raw `SetupProfiler`). Delay-load `dd-win-prof.dll`.
- Avoid gtest/spdlog/libdatadog where possible: gtest avoidable (Track A); spdlog needed to compile dd-win-prof; libdatadog needed until Track C.
- Nothing is pushed/opened/committed without explicit go-ahead at that step.

## Track A: upstream dd-win-prof PR (gtest/test targets optional)
State: branch `jward/optional-test-and-tool-targets` (commit 2cc2771, local clone `/tmp/ddwp`) is pushed to DataDog/dd-win-prof. PR #71 was opened prematurely and has been closed by the user; branch kept.
Change: options `DD_WIN_PROF_BUILD_TESTS` / `_RUNNER` / `_INJECTOR` (default ON top-level, OFF as dependency) in root `CMakeLists.txt`; `src/CMakeLists.txt` gates googletest fetch + `Tests`/`Runner`/`ProfilerInjector` subdirs; `CONTRIBUTING.md` note.
Steps:
1. Re-verify on Windows (not possible from this Mac): top-level configure+build+`Tests.exe` unchanged; `add_subdirectory`/FetchContent consumer configure downloads no gtest and defines no Tests/Runner targets.
2. Confirm with user, then open a fresh PR from the existing branch (or reopen #71): body per repo template, Windows checkboxes only checked if actually run.
3. After merge, record the merge-commit SHA for the Track B pin.
Fallback if upstream declines: tolerate gtest download (EXCLUDE_FROM_ALL), or consume the release zip.

## Track B: SDK integration (this repo, branch `jward/dd-win-prof-integration`)
Files: `CMakeLists.txt`, `src/CMakeLists.txt`, new `cmake/profiler.cmake` (model on #228's), `cmake/DatadogConvenience.cmake`, `cmake/DatadogConfig.cmake.in`, `tests/CMakeLists.txt`, `.gitlab-ci.yml`, docs, `LICENSE-3rdparty.csv`. Reuse: crashpad include pattern in `src/CMakeLists.txt` (after ddsdkcpp configured), `datadog_enable/datadog_install` crashpad_handler handling, dd-win-prof's `dd_win_prof_copy_runtime_deps`.
Steps:
1. Root: `option(DD_ENABLE_PROFILING OFF)`; internal active flag (WIN32 only; status msg otherwise); error if not x64; `cmake_minimum_required(3.28)`.
2. `cmake/profiler.cmake`: FetchContent dd-win-prof @ SHA with `EXCLUDE_FROM_ALL` (+ `DD_WIN_PROF_SOURCE_DIR` local override); expose only `dd-win-prof.h` via a dedicated include dir (avoid shadowing `Log.h`/`Uuid.h`/`version.h`); `target_link_libraries(ddsdkcpp PRIVATE dd-win-prof delayimp)`; `/DELAYLOAD:dd-win-prof.dll` as INTERFACE link option when ddsdkcpp is static, PRIVATE when shared; private `DD_ENABLE_PROFILING=1` define.
3. Install/export: can't `install(TARGETS dd-win-prof)` from another dir -> install DLLs as files to `bin`, link via `$<BUILD_INTERFACE:...>`, imported delay-load linkage + `DATADOG_BUILT_WITH_DD_ENABLE_PROFILING` in `DatadogConfig.cmake.in`; package script includes DLLs.
4. `datadog_enable` copies `dd-win-prof.dll` + `datadog_profiling_ffi.dll`; `datadog_install` installs them; doc comments updated. Test binary copies them when active.
5. CI: `build-windows-profiler` job (Release, MD, inprocess, flag ON). Default jobs unchanged.
6. Docs + license CSV (dd-win-prof, libdatadog, spdlog).
7. Open the SDK PR only after the user says so. Out of scope: Feature/API code and the missing-DLL guard (#228's rebase).
Depends on Track A merging (for the SHA pin); until then develop against `DD_WIN_PROF_SOURCE_DIR=/tmp/ddwp` on the Track A branch.

## Track C: remove libdatadog (separate; summary)
dd-win-prof touches libdatadog in 4 files (`PprofAggregator`, `ProfileExporter`, `Symbolication`, `LibDatadogHelper.h`, ~55 `ddog_*` symbols): profile interning + pprof serialize, and multipart export with `event.json`. SDK has `IHttpClient`, body writers, JSON writers, upload thread; lacks multipart, compression, protobuf, a large-payload path.
Phases (each its own PR, opened only on user go-ahead):
0. spec + golden fixtures (libdatadog v19 source, echo server)
1. libdatadog-free data model + `IProfileSink` seam in dd-win-prof (upstream PR)
2. pprof encoder in `src/datadog/impl/profiling/pprof/` (SDK PR)
3. exporter: multipart, event.json, consent, send path (SDK PR)
4. versioned sample-callback C API in dd-win-prof (upstream PR)
5. `DD_WIN_PROF_USE_LIBDATADOG` option; SDK build sets OFF; drop ffi DLL (upstream + SDK PRs)
6. A/B soak + cutover
Risks: pprof subtleties, intake compression/size contract, lost libdatadog behaviors (retry/proxy/TLS), memory vs small-batch queue, cross-repo buy-in.

## Verification
- Mac: default configure + `cmake --build build --target check-all` unchanged; `-DDD_ENABLE_PROFILING=ON` configures, prints ignore message, fetches nothing.
- Windows (CI or a Windows box): flag ON builds `ddsdkcpp` + tests, DLLs present next to test exe, install tree has both DLLs, x86 configure errors, `dd-win-prof.h` compiles under C++17, consumer configure fetches no gtest.
- Track C: decoded-pprof comparison vs libdatadog output; backend A/B.

## Unresolved questions
- Track A: new PR or reopen #71? Ticket prefix (`PROF-xxxxx`) wanted in title/commit?
- Keep a libdatadog build in dd-win-prof for standalone use (Track C phase 5) or delete upstream?
- SDK send path for profiles: on-disk upload queue (retry/consent) vs direct send?
- Symbolication stays in dd-win-prof (assumed); hand-rolled protobuf OK; compression dependency (zlib/zstd vs what vendored libcurl brings)?
- Missing-DLL guard for delay-load: confirm it belongs in #228's feature code, not this change.
