# Codebase Structure

**Analysis Date:** 2026-02-19

## Directory Layout

```
dd-sdk-cpp/
├── include-c/                 # C API headers (C99 compatible)
│   └── datadog/
│       ├── datadog.h          # Single include for C API
│       ├── core.h
│       ├── logging.h
│       ├── rum.h
│       └── crash_reporting.h
├── include-cpp/               # C++ API headers (C++17)
│   └── datadog/
│       ├── datadog.hpp        # Single include for C++ API
│       ├── core.hpp           # Core config, lifecycle, diagnostics
│       ├── logging.hpp        # Logging feature API
│       ├── rum.hpp            # RUM feature API
│       ├── crash_reporting.hpp
│       ├── attribute.hpp      # Attribute type system
│       ├── uuid.hpp
│       ├── timestamp.hpp
│       └── version.hpp
├── src/
│   ├── datadog/
│   │   ├── c/                 # C API implementation (wrappers)
│   │   ├── cpp/               # C++ API implementation
│   │   │   ├── core.cpp
│   │   │   ├── logging.cpp
│   │   │   ├── rum.cpp
│   │   │   ├── crash_reporting.cpp
│   │   │   ├── attribute.cpp
│   │   │   └── uuid.cpp
│   │   └── impl/              # Implementation details
│   │       ├── core/          # Core infrastructure
│   │       ├── features/      # Feature implementations
│   │       ├── platform/      # OS/runtime abstractions
│   │       ├── attribute/     # Attribute system
│   │       ├── json/          # JSON serialization primitives
│   │       ├── events/        # Shared event definitions
│   │       ├── diagnostics.hpp
│   │       ├── assert.hpp
│   │       └── error_message.hpp
│   └── version/               # Version info generation
├── tests/
│   ├── impl/                  # Implementation layer unit tests
│   ├── cpp/                   # C++ API integration tests
│   ├── c/                     # C API integration tests
│   ├── mock/                  # Mock implementations of platform interfaces
│   └── support/               # Test utilities and fixtures
├── examples/
│   ├── cpp/main.cpp           # C++ usage example
│   ├── c/main.c               # C usage example
│   └── repl/                  # Interactive REPL for testing
├── cmake/                     # CMake build system modules
├── tools/                     # Development utilities (print-system-info, etc.)
├── CMakeLists.txt             # Top-level build configuration
├── README.md
└── CONTRIBUTING.md
```

## Directory Purposes

**include-c/datadog/:**
- Purpose: Public C API surface (C99 compliant)
- Contains: Opaque pointer types, function declarations, enum definitions
- Key files: `datadog.h` (umbrella header), feature-specific headers

**include-cpp/datadog/:**
- Purpose: Public C++ API surface (C++17 idiomatic)
- Contains: Class templates, configuration structs, inline utilities
- Key files: `datadog.hpp` (umbrella header), `core.hpp` (Core config and lifecycle), feature headers

**src/datadog/c/:**
- Purpose: C API implementation; thin wrappers around C++ implementation
- Contains: Pointer wrapping, memory management, error propagation
- Pattern: Each C header has corresponding `.c` implementation file

**src/datadog/cpp/:**
- Purpose: C++ API implementation; bridges public API to impl layer
- Contains: Implementation of public API classes/functions, delegation to impl::Core/Feature
- Key files: `core.cpp` (Core::Create, lifecycle), `rum.cpp` (Rum::Register), `logging.cpp` (Logging::Register)

**src/datadog/impl/core/:**
- Purpose: Core infrastructure independent of features
- Contains: Core state machine, storage/upload threads, feature registry, batch writer, event queue
- Key files:
  - `core.hpp/.cpp` - Core class, initialization, thread spawning
  - `storage_thread.hpp/.cpp` - Reads event queue, writes to storage
  - `upload_thread.hpp/.cpp` - Reads batches, generates HTTP requests
  - `feature.hpp` - Feature base class contract
  - `feature_scope.hpp` - Command queue per feature
  - `storage_write.hpp` - BatchWriter for persistent batching
  - `queue.hpp` - Thread-safe event queue (SPMC)
  - `context.hpp` - HTTP context (headers, URL, auth)
  - `tlv.hpp` - Binary TLV codec for event serialization

**src/datadog/impl/features/:**
- Purpose: Feature-specific implementations
- Subdirectories:
  - `rum/` - RUM feature (scopes, commands, event generation)
  - `logging/` - Logging feature (logger, log level filtering)
  - `crash_reporting/` - Crash handler registration and reporting
- Pattern: Each feature inherits from `Feature`, implements event processing and upload

**src/datadog/impl/features/rum/scopes/:**
- Purpose: RUM scope hierarchy modeling user interaction state
- Key files:
  - `application.hpp` - Top-level scope, manages sessions
  - `session.hpp` - Session scope, manages views/resources, handles expiry
  - `view.hpp` - View scope, manages actions/resources within view
  - `resource.hpp` - Resource (network request) tracking
  - `action.hpp` - User action tracking

**src/datadog/impl/platform/:**
- Purpose: Abstract OS/runtime differences
- Interface files (define contracts):
  - `clock.hpp` - Monotonic and wall clock access
  - `filesystem.hpp` - Directory/file I/O
  - `http.hpp` - HTTP client creation and request execution
  - `system_info.hpp` - Device/OS metadata
- Implementation files (platform-specific):
  - `clock_std.cpp` - Standard library clock
  - `filesystem_std.cpp` - Standard library filesystem
  - `http_curl.cpp` - libcurl-based HTTP client
  - `system_info_linux.cpp` - Linux implementation
  - `system_info_macos.cpp` - macOS implementation
  - `system_info_windows.cpp` - Windows implementation
  - `crash_handler_noop.cpp` - No-op crash handler
  - `crash_handler_inprocess_posix.cpp` - Signal-based crash handler (POSIX)
  - `crash_handler_inprocess_windows.cpp` - SEH-based crash handler (Windows)
  - `crash_handler_crashpad.cpp` - Crashpad integration

**src/datadog/impl/attribute/:**
- Purpose: Attribute type system and serialization
- Key files:
  - `typed_attribute.hpp` - Runtime type wrapper for attributes
  - `cow.hpp` - Copy-on-write attribute map for efficient merging
  - `merge.hpp` - Attribute merge logic (global < view < command)

**src/datadog/impl/json/:**
- Purpose: JSON serialization primitives
- Key files:
  - `json.hpp` - JSON value wrapper
  - `primitives/` - Encoder implementations for each JSON type (string, number, bool, null, uuid, timestamp)
  - `attribute.cpp` - Attribute to JSON conversion

**src/datadog/impl/events/:**
- Purpose: Shared event type definitions and serialization contracts
- Contains: Enums and constants referenced by multiple features

**tests/impl/:**
- Purpose: Unit tests for implementation layer components
- Organization:
  - `tests/impl/core/` - Tests for Core, storage, upload, batching logic
  - `tests/impl/features/rum/` - Tests for RUM scopes, commands
  - `tests/impl/attribute/` - Tests for attribute merging, serialization
  - `tests/impl/json/` - Tests for JSON encoding

**tests/mock/:**
- Purpose: Mock implementations of platform interfaces for testing
- Key files:
  - `clock.hpp` - Deterministic fake clock
  - `filesystem.hpp` - In-memory filesystem
  - `http_client.hpp` - Mock HTTP client capturing requests
  - `feature.hpp` - Mock Feature for testing Core

**tests/support/:**
- Purpose: Test infrastructure and helpers
- Key files:
  - `core.hpp` - Helper to construct Core in tests
  - Catch2 integration, assertion macros

**examples/cpp/:**
- Purpose: Demonstrate C++ API usage
- Key file: `main.cpp` - Complete example showing Core creation, feature registration, event generation

**examples/c/:**
- Purpose: Demonstrate C API usage
- Key file: `main.c` - Complete example showing C API equivalent to C++ example

**examples/repl/:**
- Purpose: Interactive REPL for SDK testing and experimentation
- Key files:
  - `main.cpp` - REPL shell
  - `commands_*.cpp` - Command implementations for each feature/component
  - `state.hpp` - REPL application state

**cmake/:**
- Purpose: Build system configuration
- Key files:
  - `compile.cmake` - Compiler flags, optimization, warnings
  - `external.cmake` - External dependency acquisition (zlib, libcurl, catch2, etc.)
  - `sanitizers.cmake` - Address/UB/Thread sanitizer setup
  - `clang-format.cmake` - Code formatting targets
  - `clang-tidy.cmake` - Static analysis targets
  - `coverage.cmake` - Code coverage reporting
  - `llvm-tools.cmake` - LLVM tool installation

## Key File Locations

**Entry Points:**
- `include-cpp/datadog/datadog.hpp` - C++ public API umbrella include
- `include-c/datadog/datadog.h` - C public API umbrella include
- `src/datadog/cpp/core.cpp` - Core::Create() implementation
- `include-cpp/datadog/core.hpp` - Core class definition

**Configuration:**
- `include-cpp/datadog/core.hpp` - CoreConfig struct with setter methods
- `include-cpp/datadog/rum.hpp` - RumConfig struct
- `include-cpp/datadog/logging.hpp` - LoggerConfig struct

**Core Logic:**
- `src/datadog/impl/core/core.hpp` - impl::Core class (feature registry, thread control)
- `src/datadog/impl/core/storage_thread.hpp` - Event batching and disk I/O
- `src/datadog/impl/core/upload_thread.hpp` - Periodic upload scheduling and HTTP requests
- `src/datadog/impl/core/feature.hpp` - Feature base class contract

**RUM Implementation:**
- `src/datadog/impl/features/rum/rum.hpp` - RUM feature class
- `src/datadog/impl/features/rum/scopes/application.hpp` - Application scope (session management)
- `src/datadog/impl/features/rum/scopes/session.hpp` - Session scope (view management, expiry)
- `src/datadog/impl/features/rum/scopes/view.hpp` - View scope (action/resource tracking)

**Logging Implementation:**
- `src/datadog/impl/features/logging/logging.hpp` - Logging feature
- `src/datadog/impl/features/logging/logger.hpp` - Logger class

**Platform Abstractions:**
- `src/datadog/impl/platform/clock.hpp` - IClock interface
- `src/datadog/impl/platform/filesystem.hpp` - IStorageDirectory, IDirectory interfaces
- `src/datadog/impl/platform/http.hpp` - IHttpClient, IHttpSubsystem interfaces
- `src/datadog/impl/platform/system_info.hpp` - ISystemInfo interface

**Testing:**
- `tests/impl/core/core_test.cpp` - Core lifecycle and feature registration tests
- `tests/impl/core/storage_thread_test.cpp` - Storage batching behavior
- `tests/impl/core/upload_thread_test.cpp` - Upload scheduling and HTTP generation
- `tests/mock/` - Mock implementations for testing

## Naming Conventions

**Files:**
- Implementation headers: `.hpp` (C++17 headers in include-cpp/, implementation headers in src/)
- Implementation source: `.cpp`
- C headers: `.h` (C99 compliant)
- C source: `.c`
- Test files: `{component}_test.cpp` (co-located with source in parallel tests/ tree)
- Mock files: `mock/{component}.hpp` (headers only, in tests/mock/)

**Directories:**
- Public API headers: `include-{c,cpp}/datadog/`
- Private implementation: `src/datadog/impl/`
- Features: `src/datadog/impl/features/{feature_name}/`
- RUM scopes: `src/datadog/impl/features/rum/scopes/`
- Platform implementations: `src/datadog/impl/platform/`
- Tests: `tests/{layer}/{component}/{file}_test.cpp`

**Types & Functions:**
- Public API classes: PascalCase (e.g., `Core`, `Rum`, `Logging`)
- Public API structs: PascalCase (e.g., `CoreConfig`, `RumConfig`)
- Private implementation classes: PascalCase in `impl::` namespace (e.g., `impl::Core`)
- Enum types: PascalCase (e.g., `TrackingConsent`, `DiagnosticLevel`)
- Enum values: PascalCase (e.g., `TrackingConsent::Granted`)
- Functions: camelCase or snake_case depending on context (C API uses snake_case for compatibility)
- Member variables: snake_case with leading underscore in private sections (e.g., `_impl`, `_state`)

## Where to Add New Code

**New Feature:**
- Create directory: `src/datadog/impl/features/{feature_name}/`
- Implement feature class: `src/datadog/impl/features/{feature_name}/{feature_name}.hpp/cpp`
  - Inherit from `Feature` base class
  - Implement `GetId()`, `GetName()`, `Start()`, `Stop()`, `UploadThread_PrepareReport()`
  - Call `WriteEvent()` for each event generated
- Create public API: `include-cpp/datadog/{feature_name}.hpp`
  - Define config struct, public API class, Register() static method
- Create C wrapper: `include-c/datadog/{feature_name}.h` + `src/datadog/c/{feature_name}.c`
- Add tests: `tests/impl/features/{feature_name}/{feature_name}_test.cpp`
- Register feature in Core: Feature calls `RegisterFeature()` in `Start()`

**New RUM Scope Type:**
- Add scope class: `src/datadog/impl/features/rum/scopes/{scope_name}.hpp/cpp`
  - Inherit from base scope, implement `Process()`, `ShouldCloseRatherThanProcessing()`
  - Define `RumCommand` subtypes for events targeting this scope
- Update command dispatch: `src/datadog/impl/features/rum/scopes/parent_scope.hpp` to handle new scope
- Add tests: `tests/impl/features/rum/scopes/{scope_name}_test.cpp`

**New Logging or RUM Method:**
- Add method to public API: `include-cpp/datadog/{logging,rum}.hpp`
- Implement in feature: `src/datadog/impl/features/{logging,rum}/{logging,rum}.hpp/cpp`
  - May create new command type: `src/datadog/impl/features/{logging,rum}/command.hpp`
  - Call `WriteEvent()` or dispatch command to scope
- Add C wrapper: `include-c/datadog/{logging,rum}.h` + `src/datadog/c/{logging,rum}.c`
- Add tests: `tests/cpp/{logging,rum}_test.cpp` or `tests/impl/features/{logging,rum}/{method}_test.cpp`

**Utilities/Helpers:**
- Shared attribute logic: `src/datadog/impl/attribute/`
- Shared event logic: `src/datadog/impl/events/`
- JSON encoding: `src/datadog/impl/json/primitives/`
- Diagnostic logging: Use `DiagnosticLogger` from `src/datadog/impl/diagnostics.hpp`

## Special Directories

**src/datadog/impl/core/feature_types/:**
- Purpose: Feature-specific type definitions generated/managed by core
- Generated: Partial code generation from schema
- Committed: Yes

**src/version/:**
- Purpose: Build-time version information injection
- Generated: Version string computed from CMake/git
- Committed: No (generated at build time)

**cmake/:**
- Purpose: CMake modules for build system
- Generated: Some files like DatadogConfig.cmake generated at build time
- Committed: Yes (templates and drivers; outputs are build artifacts)

**.datadog/:**
- Purpose: Runtime storage directory (created by SDK when it starts)
- Generated: Yes (at runtime)
- Committed: No (gitignored)
