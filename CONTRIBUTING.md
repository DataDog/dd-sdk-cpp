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

If you enable either tool in your CMake configuration (via `DD_ENABLE_CLANG_TIDY` and `DD_ENABLE_CLANG_FORMAT`), then the required versions of these tools must be present in your PATH. If you enable `DD_DEVELOPMENT_ALLOW_AUTO_INSTALL`, CMake will automatically download these tools to `llvm-bin/` if necessary.

Alternatively, you can install the appropriate LLVM/Clang release and ensure that its `bin/` directory is in your PATH.

### Configuring your CMake build

If you want to make changes to SDK's source, you can configure your CMake build using the `DD_DEVELOPMENT` option:

```
cmake -DDD_DEVELOPMENT=ON -S . -B build
```

Other important options include:

- `-DCMAKE_BUILD_TYPE=Debug` (or `Release`, or `RelWithDbgInfo`)
- `-DBUILD_SHARED_LIBS=ON` will build the SDK as a shared library; use `OFF` for a static library build
- `-DDD_HTTP_USE_SYSTEM_LIBCURL=ON` will link the SDK dynamically against the version of libcurl installed on your system; use `OFF` to download libcurl, build it from source, and link it into the `dd-sdk-cpp` binary
- `-DDD_DEVELOPMENT_ALLOW_AUTO_INSTALL=ON` will permit the CMake configuration process to download **clang-format** and **clang-tidy** if not already present on your system
- `-DDD_ENABLE_CLANG_FORMAT=OFF` and `-DDD_ENABLE_CLANG_TIDY=OFF` will omit those tools from the build

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

Unit tests are written using [Catch2](https://github.com/catchorg/Catch2). If you configure with `DD_DEVELOPMENT` (or `DD_BUILD_TESTING`), the build will produce a Catch2 binary at `./build/tests/dd_native_tests`. You may run that binary directly, or use CTest, to invoke the test suite:

```
ctest --test-dir build
```

### Running benchmarks

If you configure with `DD_DEVELOPMENT` (or `DD_BUILD_BENCHMARKS`), the build will produce a program at `./build/benchmarks/dd_native_benchmark`, which you can use to run a small set of benchmarks that exercise SDK functionality.

For example, to profile SDK startup time:

```
build/benchmarks/dd_native_benchmark startup
```

## Repository overview

- [`include-c/`][include-c] contains public headers for the C API.
- [`include-cpp/`][include-cpp] contains public headers for the C++ API.
- [`src/c/`][src-c] implements the C API, binding it to `src/impl/`.
- [`src/cpp/`][src-cpp] implements the C++ API, binding it to `src/impl/`.
- [`src/impl/`][src-impl] implements the core business logic of the library, split into:
    - [`core/`][impl-core]: Implements the primary business logic of the SDK, including:
        - [`core.hpp`][core-hpp]: Internal core of the SDK, which handles initialization, feature registration, and which runs the storage and upload threads.
        - [`feature.hpp`][feature-hpp]: Interfaces used to implement specific features, allowing a feature implementation to register itself with the core, define how it generates events for storage and processes them for upload, and implement its user-facing API.
        - [`storage_thread.hpp`][storage-thread-hpp]: Business logic of the storage thread, which asynchronously writes event payloads to disk, batched into TLV-formatted binary files, as those events are generated by feature implementations.
        - [`upload_thread.hpp`][upload-thread-hpp]: Business logic of the upload thread, which periodically reads batches of events from storage, passes them to the appropriate feature implementation for processing, then sends them to intake over HTTP.
    - [`features/`][impl-features]: Individual, independent modules for specific features:
        - [`logging/`][impl-logging]: Allows users to create loggers; generates log message events in response to log calls.
    - [`platform/`][impl-platform]: Platform abstraction layer, i.e. modular subsystems for platform-specific functionality like filesystem access and HTTP client.
- [`tests/`][tests]: Unit tests for code in `src/`, along with test-only support code.
- [`examples/`][examples] demonstrates usage of both C and C++ APIs.
- [`benchmarks/`][benchmarks] implements small, self-contained commands that exercise SDK functionality in a controlled environment.

[include-c]: ./include-c/
[include-cpp]: ./include-cpp/
[src-c]: ./src/c/
[src-cpp]: ./src/cpp/
[src-impl]: ./src/impl/
[impl-core]: ./src/impl/core/
[core-hpp]: ./src/impl/core/core.hpp
[feature-hpp]: ./src/impl/core/feature.hpp
[storage-thread-hpp]: ./src/impl/core/storage_thread.hpp
[upload-thread-hpp]: ./src/impl/core/upload_thread.hpp
[impl-features]: ./src/impl/features/
[impl-logging]: ./src/impl/features/logging/
[impl-platform]: ./src/impl/platform/
[tests]: ./tests/
[examples]: ./examples/
[benchmarks]: ./benchmarks/

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
