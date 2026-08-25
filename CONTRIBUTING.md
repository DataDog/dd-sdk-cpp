# Contributing

First of all, thanks for contributing!

This document provides some basic guidelines for contributing to this repository. To propose improvements, feel free to [submit a PR](https://github.com/DataDog/dd-sdk-cpp/pulls) or [open an Issue](https://github.com/DataDog/dd-sdk-cpp/issues).

**Note:** Datadog requires that all commits within this repository must be signed, including those within external contribution PRs. Please ensure you have followed GitHub's [Signing Commits](https://docs.github.com/en/authentication/managing-commit-signature-verification/signing-commits) guide before proposing a contribution. PRs lacking signed commits will not be processed and may be rejected.

## Found a bug?

For any urgent matters (such as outages) or issues concerning the Datadog service or UI, contact our support team via https://docs.datadoghq.com/help/ for direct, faster assistance.

You may submit a bug report concerning the Datadog C++ SDK by [opening a GitHub Issue](https://github.com/DataDog/dd-sdk-cpp/issues). Use the appropriate template and provide all listed details to help us resolve the issue.

## Development instructions

### Prerequisite tools

To build the Datadog C++ SDK, you'll need [CMake](https://cmake.org/) 3.21 or newer, along with a supported C++ compiler toolchain for your operating system. We test our builds against the following compiler versions:

- On **macOS:** Apple Clang 15, installed with Xcode 15.3
- On **Linux:** Clang 15, Clang 20, GCC 11, and GCC 13, all with libstdc++
- On **Windows:** Microsoft Visual C++ 17.14 (vc143), installed with Visual Studio 2022

### Optional tools

We use [**clang-format**](https://clang.llvm.org/docs/ClangFormat.html) for automatic source code formatting, and we use [**clang-tidy**](https://clang.llvm.org/extra/clang-tidy/) for static analysis. These tools are not strictly required in order to build the SDK, but you should use them if you intend to submit a PR.

Regardless of the compiler toolchain used to build the SDK, all **clang-format** and **clang-tidy** checks are done using [**version 20.1.8**](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8) of these tools.

If you enable either tool in your CMake configuration (via `DD_ENABLE_CLANG_TIDY` and `DD_ENABLE_CLANG_FORMAT`), the required versions must be resolvable: either present in your `PATH`, or already installed under `llvm-tools/` (e.g. from a prior auto-install). If you set `-DDD_DEVELOPMENT_ALLOW_AUTO_INSTALL=ON`, CMake will download them to `llvm-tools/` automatically.

Alternatively, you can install the appropriate LLVM/Clang release and ensure that its `bin/` directory is in your PATH.

### Configuring your CMake build

If you want to make changes to SDK's source, you can configure your CMake build using the `DD_DEVELOPMENT` option:

```
cmake -DDD_DEVELOPMENT=ON -S . -B build
```

`DD_DEVELOPMENT=ON` is a convenience flag that enables the options you typically want while working on the SDK itself: it builds the examples, tests, and developer tools; enables asserts; and turns on clang-format, clang-tidy, coverage instrumentation, and sanitizers (ASan + UBSan by default). You can override any of these individually with the options listed below.

Other important options include:

- `-DCMAKE_BUILD_TYPE=Debug` (or `Release`, or `RelWithDbgInfo`)
- `-DBUILD_SHARED_LIBS=ON` will build the SDK as a shared library; use `OFF` for a static library build
- `-DDD_HTTP_USE_SYSTEM_LIBCURL=ON` will link the SDK dynamically against the version of libcurl installed on your system; use `OFF` to download libcurl, build it from source, and link it into the `dd-sdk-cpp` binary
- `-DDD_DEVELOPMENT_ALLOW_AUTO_INSTALL=ON` will permit the CMake configuration process to download **clang-format** and **clang-tidy** if not already present on your system
- `-DDD_ENABLE_CLANG_FORMAT=OFF` and `-DDD_ENABLE_CLANG_TIDY=OFF` will omit those tools from the build
- `-DDD_ENABLE_COVERAGE=OFF` will disable `llvm-cov` instrumentation in environments where it's unsupported or undesirable
- `-DDD_ENABLE_SANITIZERS=` will disable sanitizers (useful for testing crash behavior etc.); default is `ASan,UBSan`; `TSan` and/or `MSan` can also be used as alternatives

For a complete list of configuration options, see [`CMakeLists.txt`](./CMakeLists.txt).

### Building the SDK

To run the build once you've configured a CMake `build` directory, you can use CMake to start a build:

```
cmake --build build
```

Note that when using Visual Studio, the build configuration must be passed at build-time:

```
cmake --build build --config Debug
```

### Running tests

Unit tests are written using [Catch2](https://github.com/catchorg/Catch2). If you configure with `DD_DEVELOPMENT` (or `DD_BUILD_TESTING`), the build will produce a Catch2 binary at `./build/tests/tests`. You may run that binary directly, or use CTest, to invoke the test suite:

```
ctest --test-dir build
```

### Formatting and linting your code

Before opening a PR, format and lint your changes. CI runs the equivalent of these commands and will fail on any diff.

First-time setup — configures the build and, if clang-format / clang-tidy 20.1.8 are not already installed, downloads them into `llvm-tools/`. `DD_DEVELOPMENT=ON` already implies `DD_ENABLE_CLANG_FORMAT=ON` and `DD_ENABLE_CLANG_TIDY=ON`; they are passed explicitly below so this command also fixes an existing `build/` that was previously configured with them disabled:

```
cmake -DDD_DEVELOPMENT=ON \
      -DDD_ENABLE_CLANG_FORMAT=ON \
      -DDD_ENABLE_CLANG_TIDY=ON \
      -DDD_DEVELOPMENT_ALLOW_AUTO_INSTALL=ON \
      -S . -B build
```

Note: the auto-install downloads the full LLVM release tarball (~1 GB on Linux, ~1.4 GB on macOS), so the first configure can take several minutes.

Format all SDK sources in-place:

```
cmake --build build --target format
```

Verify formatting without modifying files (what CI runs):

```
cmake --build build --target check-format
```

Verify formatting, build the tests (which runs clang-tidy at compile time via `CXX_CLANG_TIDY`), and run CTest — a single command that mirrors CI:

```
cmake --build build --target check-all
```

On Windows, the formatting targets do not require a `--config` flag (they are configuration-agnostic custom targets).

#### Troubleshooting

- **`No rule to make target 'format'`** — you are on an older checkout or the build was configured with `DD_ENABLE_CLANG_FORMAT=OFF`. Reconfigure using the command above.
- **`The 'format' target is not available in this build`** — clang-format support is disabled or could not be resolved. Follow the hint in the error message, or reconfigure as above.
- **Hash mismatch during LLVM download** — delete `llvm-tools/` and reconfigure; the auto-installer retries once automatically but will give up after a second failure.
- **Want to skip these tools entirely?** — configure with `-DDD_ENABLE_CLANG_FORMAT=OFF -DDD_ENABLE_CLANG_TIDY=OFF`. Your PR will then need to pass these checks in CI; if you are an external contributor, it is safer to run them locally first.

## Repository overview

- [`include-c/`][include-c] contains public headers for the C API.
- [`include-cpp/`][include-cpp] contains public headers for the C++ API.
- [`src/datadog/c/`][src-c] implements the C API.
- [`src/datadog/cpp/`][src-cpp] implements the C++ API.
- [`src/datadog/impl/`][src-impl] implements the core business logic of the library, split into:
    - [`types/`][impl-types] implements essential data types and related routines used across modules, including:
        - [`json/`][types-json]: Minimal JSON serialization routines for encoding event data and attribute values
        - [`events/`][types-events]: Utilities used to build JSON-serializable struct types for event payloads
        - [`diagnostics.hpp][types-diagnostics]: Internal code used when the SDK needs to log local-only diagnostic messages
        - [`logging.hpp`][types-logging]: Logging-related data structures used across module boundaries
        - [`rum.hpp`][types-rum]: RUM-related data structures, including RUM events adhering to [rum-events-format](https://github.com/DataDog/rum-events-format/), used across modules
    - [`core/`][impl-core] implements the primary business logic of the SDK, including:
        - [`util/`][core-util]: Internal utility code for commonly-used functionality
        - [`attribute/`][core-attribute]: Copy-on-Write implementation and other utilities used in conjunction with API-layer `datadog::Attribute` type
        - [`storage/`][core-storage]: Code used to access the filesystem, prepare the SDK's `.datadog/` storage directory, and write and migrate event data
        - [`platform/`][core-platform]: Implementations of platform-specific functionality like system info, HTTP client, and access to the system clock
        - [`core.hpp`][core-hpp]: Internal core of the SDK, which handles initialization, feature registration, and which runs the storage and upload threads.
        - [`context.hpp`][context-hpp]: Common types like `CoreContext`, which provides all features with a snapshot of the SDK's essential state.
        - [`feature.hpp`][feature-hpp]: Interfaces used to implement specific features, allowing a feature implementation to register itself with the core, define how it generates events for storage and processes them for upload, and implement its user-facing API.
        - [`feature_scope.hpp`][feature-scope-hpp]: Definition of `FeatureScope`, the interface that gives each feature to access to Core functionality required to access and update context, generate events, etc.
        - [`context_thread.hpp`][context-thread-hpp]: The context thread, where work initiated by API calls is enqueued to be processed in the background (so as not to block the calling thread), ultimately modifying `CoreContext` and/or sending events to the storage thread.
        - [`storage_thread.hpp`][storage-thread-hpp]: The storage thread, which asynchronously writes event payloads to disk, batched into TLV-formatted binary files, as those events are generated by feature implementations.
        - [`upload_thread.hpp`][upload-thread-hpp]: The upload thread, which periodically reads batches of events from storage, passes them to the appropriate feature implementation for processing, then sends them to intake over HTTP.
        - [`messaging_thread.hpp`][messaging-thread-hpp]: The messaging thread, where messages are routed and handled, allowing features to respond to state changes that have occurred in the Core or in other features.
    - [`logging/`][impl-logging] allows an application to create loggers which will send messages to Datadog.
    - [`rum/`][impl-rum] allows an application to keep track of application state and user interactions via Views, Actions, Resources, etc., sending RUM events to Datadog.
    - [`crash_reporting/`][impl-crash-reporting] automatically detects application crashes and sends crash reports to Datadog as RUM Errors.
        - Multiple crash-handling mechanisms are planned; currently only `DD_CRASH_MODE=inprocess` is supported.
- [`tests/`][tests]: Unit tests for code in `src/`, along with test-only support code.
- [`examples/`][examples] demonstrates usage of both C and C++ APIs.

[include-c]: ./include-c/
[include-cpp]: ./include-cpp/
[src-c]: ./src/datadog/c/
[src-cpp]: ./src/datadog/cpp/
[src-impl]: ./src/datadog/impl/
[impl-types]: ./src/datadog/impl/types/
[types-json]: ./src/datadog/impl/types/json/
[types-events]: ./src/datadog/impl/types/events/
[types-diagnostics]: ./src/datadog/impl/types/diagnostics.hpp
[types-logging]: ./src/datadog/impl/types/logging.hpp
[types-rum]: ./src/datadog/impl/types/rum.hpp
[impl-core]: ./src/datadog/impl/core/
[core-util]: ./src/datadog/impl/core/util/
[core-attribute]: ./src/datadog/impl/core/attribute/
[core-feature-types]: ./src/datadog/impl/core/feature_types/
[core-storage]: ./src/datadog/impl/core/storage/
[core-platform]: ./src/datadog/impl/core/platform/
[core-hpp]: ./src/datadog/impl/core/core.hpp
[context-hpp]: ./src/datadog/impl/core/context.hpp
[feature-hpp]: ./src/datadog/impl/core/feature.hpp
[feature-scope-hpp]: ./src/datadog/impl/core/feature_scope.hpp
[context-thread-hpp]: ./src/datadog/impl/core/context_thread.hpp
[storage-thread-hpp]: ./src/datadog/impl/core/storage_thread.hpp
[upload-thread-hpp]: ./src/datadog/impl/core/upload_thread.hpp
[messaging-thread-hpp]: ./src/datadog/impl/core/messaging_thread.hpp
[impl-logging]: ./src/datadog/impl/logging/
[impl-rum]: ./src/datadog/impl/rum/
[impl-crash-reporting]: ./src/datadog/impl/crash_reporting/
[tests]: ./tests/
[examples]: ./examples/

## CI

Our CI pipeline runs on GitLab, and jobs are not publicly visible.

If you have [Docker](https://www.docker.com/) installed, you can use [`docker-ci.sh`](./docker-ci.sh) to run containers that replicate the Linux build environment used in our CI pipeline.

For example, to run a development build with Clang 20 and then run the test suite:

```
./docker-ci.sh run unit-test --exit
```

Or to produce precompiled binaries using GCC 11:

```
./docker-ci.sh --toolchain gcc11 run package --clean --exit
```

Run the script with no arguments for usage information.
