# Agent instructions for dd-sdk-cpp

This file gives AI coding assistants (Claude Code, Codex, Cursor, etc.) the
minimum context needed to contribute to this repository without breaking CI.

Humans should read [`CONTRIBUTING.md`](./CONTRIBUTING.md) instead — it has
deeper context and examples. This file is deliberately short and command-
oriented so agents can follow it mechanically.

## Before declaring any code change "done"

Run, from the repo root:

```
cmake --build build --target check-all
```

This target runs, in order:

1. `check-format` — clang-format 20.1.8 in dry-run mode (CI will reject any diff).
2. A full build of the test binary — clang-tidy runs at compile time via the
   `CXX_CLANG_TIDY` target property, so tidy violations surface as build errors.
3. The unit test suite via CTest.

If `check-format` reports formatting violations, fix them with:

```
cmake --build build --target format
```

…and commit the result.

## First-time build configuration

If the `build/` directory does not yet exist, or `cmake --build build --target format`
fails with `No rule to make target 'format'`, configure (or re-configure) with:

```
cmake -DDD_DEVELOPMENT=ON \
      -DDD_ENABLE_CLANG_FORMAT=ON \
      -DDD_ENABLE_CLANG_TIDY=ON \
      -DDD_DEVELOPMENT_ALLOW_AUTO_INSTALL=ON \
      -S . -B build
```

`DD_DEVELOPMENT=ON` implies `DD_ENABLE_CLANG_FORMAT=ON` and
`DD_ENABLE_CLANG_TIDY=ON`. The flags are passed explicitly so that this
command also fixes an existing `build/` cache that was previously configured
with them disabled.

`DD_DEVELOPMENT_ALLOW_AUTO_INSTALL=ON` lets CMake download clang-format and
clang-tidy 20.1.8 into `llvm-tools/` automatically if they are not already on
`PATH`. The download is ~1 GB on Linux and ~1.4 GB on macOS — expect the first
configure to take several minutes.

If the configure fails with an LLVM archive hash mismatch, delete `llvm-tools/`
and rerun the configure. The auto-installer retries once internally, but a
persistently-corrupt download means the cached file must be removed.

## Do-not rules

- Do **not** run `clang-format` directly on individual files. Always use the
  CMake `format` / `check-format` targets so the set of formatted files
  matches CI (`examples include-c include-cpp src tests`).
- Do **not** commit changes inside `build/` or `llvm-tools/`; they are local
  build artifacts and are ignored by `.gitignore`.
- Do **not** bypass pre-commit hooks with `--no-verify`. If a hook fails,
  fix the underlying problem.
- Do **not** change CI behavior silently. If you modify `cmake/clang-format.cmake`,
  `cmake/clang-tidy.cmake`, `cmake/llvm-tools.cmake`, or `.clang-format` /
  `.clang-tidy`, call it out explicitly in the PR description.

## Repository layout (quick reference)

| Path | Purpose |
|---|---|
| `include-c/`, `include-cpp/` | Public C and C++ API headers |
| `src/datadog/impl/` | SDK implementation (core, rum, logging, crash_reporting) |
| `tests/` | Catch2 unit tests (binary: `build/tests/tests`) |
| `examples/` | Example C and C++ programs using the SDK |
| `cmake/` | Build configuration modules |
| `CONTRIBUTING.md` | Full developer guide (the canonical reference) |

## When in doubt

If these instructions conflict with `CONTRIBUTING.md`, follow `CONTRIBUTING.md`.
If `CONTRIBUTING.md` is silent on something, prefer the narrower, safer option
and note the ambiguity in the PR description.
