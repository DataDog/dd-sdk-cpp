# Agent instructions for dd-sdk-cpp

`CONTRIBUTING.md` is the full developer guide: build options, tooling setup, troubleshooting, and an annotated tour of the source tree. This file covers what an agent needs to make a change that passes CI, plus conventions that aren't enforced by tooling. If the two disagree, follow `CONTRIBUTING.md`.

## Validating a change

A change to SDK source isn't finished until it passes this, run from the repo root:

```
cmake --build build --target check-all
```

This checks formatting, builds the SDK and tests (clang-tidy runs at compile time, and its warnings are errors), runs the unit tests, and then runs the integration tests, which drive the example `repl` binary against a local proxy and assert on the requests the SDK sends. If `build/` doesn't exist, or the target is missing, configure with `DD_DEVELOPMENT=ON` as described in "Formatting and linting your code" in `CONTRIBUTING.md`. That configuration enables the integration tests, which need Python 3.12+; `check-all` sets up their venv itself.

Fix formatting with `cmake --build build --target format`. Don't run `clang-format` on individual files: the CMake targets format exactly the set of files that CI checks.

If you change RUM event generation or any test that uses `DATADOG_RUM_EVENT_LITERAL`, also run RUM event validation, which checks those literals against the rum-events-format schema. Configure with `-DDD_ENABLE_EVENT_VALIDATION=ON`, then run `cmake --build build --target validate-event-types`. It only scans the test files listed in `__cpp_test_relpaths__` in `tools/validate-event-types/main.py`, so add any new test file that uses the macro to that list.

To run a subset of unit tests, pass Catch2 tags to the binary, e.g. `build/tests/tests "[rum]"`. Each test is tagged `[unit]` plus an area such as `[rum]`, `[logging]`, `[c-api]`, or `[cpp-api]`.

`DD_DEVELOPMENT=ON` enables ASan and UBSan, which intercept some deliberate crashes before the SDK's crash handler sees them, so `check-all` skips the integration tests that depend on those crashes (CI still runs them). To test crash behavior locally, configure with `-DDD_ENABLE_SANITIZERS=`.

## Repository layout

| Path | Purpose |
|---|---|
| `include-cpp/`, `src/datadog/cpp/` | Public C++ API: headers and the thin layer implementing them |
| `include-c/`, `src/datadog/c/` | Public C API: headers and the thin layer implementing them |
| `src/datadog/impl/` | The SDK's implementation: `core/`, `rum/`, `logging/`, `crash_reporting/`, `crash_processing/`, shared `types/` |
| `tests/` | Catch2 unit tests, mirroring `src/` (`tests/c/`, `tests/cpp/`, `tests/impl/`) |
| `tools/integration-test/tests/` | Python integration tests, one scenario per file (e.g. `rum_smoke.py`, `crash_smoke_inprocess.py`) |
| `examples/` | C and C++ example programs, including the `repl` used by integration tests |
| `tools/` | Developer and release scripts (`integration-test`, `validate-event-types`, `prepare-release`, ...) |

## Design constraints

- **API calls must not block the caller.** Public API functions enqueue work to the context thread, which updates SDK state and generates events. Storage and upload run on their own threads. See "Repository overview" in `CONTRIBUTING.md` for the thread model.
- **Every public API function has both a C and a C++ surface.** A new function touches `include-cpp/` and `src/datadog/cpp/`, `include-c/` and `src/datadog/c/`, the `impl/` code behind them, and tests in both `tests/cpp/` and `tests/c/`.
- **Code reachable from the in-process crash handler must be async-signal-safe**: no heap allocation, no locks, and no calls that aren't safe from a signal handler. This covers `crash_reporting/handlers/inprocess/` and the crash report writer it calls.
- **Public headers are user-facing documentation.** `include-c/` and `include-cpp/` are read by SDK users and by the cross-platform SDKs built on dd-sdk-cpp, so document every public type and function there.

## Code style

clang-format (Google-based, 88 columns) and clang-tidy enforce formatting and many lint rules. Beyond those:

- **Naming:** types and methods are `PascalCase`; parameters and locals are `snake_case`; data members are `_snake_case`. File-local helpers are `static` functions named in `snake_case`.
- **Doc comments:** use `/** ... */` blocks for types and functions, with narrative prose that weaves parameters in with backticks and ends with return behavior ("On success, returns X. On failure, returns Y."). Use `@param`/`@return` only when a parameter's constraints would be awkward in prose. Note constraints like thread-safety or async-signal-safety inline. Omit docs on helpers whose name and body are self-explanatory.
- **Comment width:** write comments to fit 88 columns, so that clang-format doesn't reflow them awkwardly.
- **Error paths:** after an early return, add a brief comment saying what failed and why we stop: `// Invalid signature; unable to parse`.
- **Binary formats:** before code that navigates a binary layout (storage batches, crash report data, executable headers), add an ASCII diagram of offsets, structures, and sizes, with a worked example.
- **Structure:**
  - Keep detection (reading files, identifying formats or architectures) in the caller, and pass the result to a focused helper that implements the algorithm.
  - Validate inputs at function entry, checking sizes, magic numbers, and counts against sane limits.
  - Prefer descriptive intermediate variables (`section_start`, `section_end`) over complex inline expressions.
  - Define functions in a `.cpp` in the order they're declared in its `.h`, and insert new definitions at the matching position.

## Commits and PRs

- Start the first line of every commit message and PR title with `feat:`, `fix:`, or `chore:`, with no scope in parentheses: `tools/prepare-release` parses PR titles with `^(feat|fix|chore)(!)?:`.
- Add `!` only for breaking changes, and not while the major version is 0.
- If there is a Jira ticket, add it to commit messages on its own line as `refs: RUM-12345`, separated from the first line by a blank line. For a PR, put it in the description.
- Don't edit `CHANGELOG.md` in feature PRs; it's written at release time, starting from PR titles.
