# Technology Stack

**Analysis Date:** 2026-02-19

## Languages

**Primary:**
- C++ 17 (C++20 when building with Crashpad support) - SDK implementation and public API (`include-cpp/`, `src/datadog/cpp/`)
- C 99 - C API for FFI interoperability (`include-c/`, `src/datadog/c/`)

**Secondary:**
- Python 3 - Build infrastructure, crash handler bootstrapping, event validation, integration tests (`tools/bootstrap-crashpad/`, `cmake/`)
- BrightScript - CI pipeline configuration (`.gitlab-ci.yml`)

## Runtime

**Environment:**
- Cross-platform: macOS, Linux, Windows
- CMake 3.21+ required for configuration and build

**Package Manager:**
- CMake FetchContent - Dependency management for development and bundled libraries
- Conan compatible (for integration into other projects via FetchContent)

## Frameworks

**Core:**
- CMake 3.21+ - Build system and configuration (`CMakeLists.txt`, `cmake/`)
- Catch2 3.9.0 - Unit testing framework (`cmake/deps/catch2.cmake`, `tests/`)

**HTTP Client:**
- libcurl 8.11.0 - HTTP communication for data uploads (`cmake/deps/libcurl.cmake`)
  - Can be bundled statically or linked as system dependency
  - Configured with minimal features (no SSH2, PSL, LDAP, etc.)

**Crash Handling:**
- Crashpad (optional) - Cross-platform crash reporting (`cmake/crashpad.cmake`)
  - Requires C++20
  - Uses Chromium toolchain (depot_tools, gn, ninja)
  - Can be replaced with in-process or noop handlers

**Development/Validation:**
- clang-format - Code formatting (`cmake/clang-format.cmake`)
- clang-tidy - Static analysis (`cmake/clang-tidy.cmake`)
- Python event validation - JSON schema validation against rum-events-format (`cmake/event-validation.cmake`)

## Key Dependencies

**Critical:**
- libcurl 8.11.0 - HTTP transport for uploading event data to Datadog intake
- date/date.h 3.0.4 - ISO timestamp formatting (`cmake/deps/date.cmake`)
- nonstd/expected-lite 0.9.0 - C++23-style error handling backport (`cmake/deps/expected-lite.cmake`)
- Catch2 3.9.0 - Unit test framework (development only)

**Development Only:**
- nlohmann/json 3.12.0 - JSON validation against event schemas (development only, `cmake/deps/nlohmann-json.cmake`)

**Internal Headers:**
- date/date.h 3.0.4 - Chrono-compatible datetime handling
- nonstd/expected.hpp 0.9.0 - Error handling without exceptions
- Catch2 3.9.0 - Test framework and assertions

**Conditional:**
- Crashpad client library - If `DD_CRASH_MODE=crashpad`
- Python3 - If `DD_ENABLE_EVENT_VALIDATION=ON` or `DD_ENABLE_INTEGRATION_TEST=ON`

## Configuration

**Build Options:**
- `DD_BUILD_SHARED` - Build as shared library (default: OFF for static)
- `DD_CRASH_MODE` - Crash handler implementation: "noop", "inprocess", "crashpad" (default: "noop")
- `DD_BUILD_EXAMPLES` - Build example programs
- `DD_BUILD_TESTING` - Build unit tests
- `DD_ENABLE_CLANG_FORMAT` - Enable code format checking
- `DD_ENABLE_CLANG_TIDY` - Enable static analysis
- `DD_ENABLE_COVERAGE` - Enable code coverage reporting
- `DD_ENABLE_SANITIZERS` - Enable memory/UB/thread sanitizers (e.g., "ASan,UBSan", "TSan")
- `DD_ENABLE_ASSERTS` - Enable assertions in SDK
- `DD_HTTP_USE_SYSTEM_LIBCURL` - Link system libcurl vs. bundled (default: ON)
- `DD_CLOCK_USE_DEFAULT` - Use std chrono vs. custom clock implementation (default: ON)
- `DD_FILESYSTEM_USE_DEFAULT` - Use std filesystem vs. custom implementation (default: ON)
- `DD_SYSTEMINFO_USE_DEFAULT` - Use platform-specific system info vs. custom (default: ON)

**Runtime Configuration:**
- Client token (required) - Datadog application token
- Service name - Application identifier
- Environment - Deployment environment (e.g., prod, staging)
- Site - Datadog datacenter: us1, us3, us5, eu1, ap1, ap2, us1_fed
- Event storage location - Directory path for persistent event batches
- Batch size - Small/Medium/Large timing for event batching
- Upload frequency - Frequent/Average/Rare for HTTP request scheduling
- Tracking consent - Granted/NotGranted/Pending for GDPR compliance

**Environment Variables:**
- `.datadog/` subdirectory created at runtime within configured storage location
  - Contains event batch files and transient SDK state
  - Files are freely created/deleted by storage and upload threads

## Platform Requirements

**Development:**
- CMake 3.21+
- C++ compiler: clang 15+, gcc 11+, MSVC 2022 (v143)
- Python 3 (for Crashpad bootstrap, event validation, integration tests)
- libcurl development headers (if using system libcurl)
- UUID development library (Linux only; macOS has libc implementation, Windows uses CoCreateGuid)
  - Ubuntu/Debian: `uuid-dev`
  - RHEL/CentOS/Fedora: `libuuid-devel`
  - Alpine: `util-linux-dev`

**Build Configurations:**
- Static library (default)
- Shared library
- Debug and Release configurations
- Multiple C++ standards: C++17 (default), C++20 (Crashpad)

**Production:**
- macOS 15.0+
- Linux (glibc-based distributions)
- Windows 7+
- Applications embedding the SDK may distribute as static or dynamic library

## SDK Features

**Logging:**
- Public API: `datadog::Logging`, `datadog::Logger`
- C API: `dd_logging_t`, `dd_logger_t`
- Event serialization to JSON for HTTP upload

**Real User Monitoring (RUM):**
- Public API: `datadog::Rum`
- C API: `dd_rum_t`
- Scope hierarchy: Application → Session → View
- Event types: View, Action, Resource, Error, Long Task
- Feature operations with step tracking

**Crash Reporting:**
- Public API: `datadog::CrashReporting`
- C API: `dd_crash_reporting_t`
- Pluggable implementations: noop, in-process, Crashpad

**Core Infrastructure:**
- Thread-safe initialization and lifecycle management
- Storage queue with persistent batching
- Upload scheduler with configurable frequency
- Diagnostic logging with severity levels
- Attribute merging (global → view → event)

---

*Stack analysis: 2026-02-19*
