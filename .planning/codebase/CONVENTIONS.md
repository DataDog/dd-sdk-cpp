# Coding Conventions

**Analysis Date:** 2026-02-19

## Naming Patterns

**Files:**
- Headers: `.hpp` extension for C++ headers, `.h` for C API headers
- Source: `.cpp` extension for implementation files
- Test files: `{module}_test.cpp` pattern (e.g., `core_test.cpp`, `rum_test.cpp`)
- Internal implementation headers in `src/datadog/impl/` are not part of public API

**Functions:**
- PascalCase for public functions and methods (e.g., `StartView()`, `SetTrackingConsent()`)
- snake_case for internal/private helper functions (e.g., `_make_core()`, `_fs_error_string()`)
- Prefixed with underscore for private members (e.g., `_config`, `_state`, `_storage_queue`)

**Variables:**
- camelCase for local variables and parameters (e.g., `clock`, `storage`, `config`)
- snake_case for private member variables (e.g., `_core`, `_diagnostic_logger`, `_features`)
- UPPER_CASE for constants (e.g., `APPLICATION_ID`, `CORE_CONFIG`)
- Hungarian notation for output parameters: `out_` prefix (e.g., `out_url`, `out_headers`, `out_core`)

**Types:**
- PascalCase for classes and structs (e.g., `Core`, `MockFeature`, `BatchWriter`)
- PascalCase for enums (e.g., `CoreState`, `DiagnosticLevel`, `TrackingConsent`, `RumActionType`)
- snake_case for namespace names (e.g., `datadog::impl`, `datadog::platform`)

**Test Naming:**
- UPPER_CASE for test macros and fixtures (e.g., `TEST_CASE`, `SECTION`, `REQUIRE`, `MOCK_CORE_CONFIG`)
- Descriptive section names following pattern: "M [expected behavior] W [given condition]" (e.g., "M create Core in Uninitialized state W constructor called")

## Code Style

**Formatting:**
- Tool: clang-format (Google style with customizations)
- Column limit: 88 characters for readability in side-by-side review
- Block indentation for function arguments when split across lines
- Each parameter on its own line when argument list is too long (`BinPackArguments: false`, `BinPackParameters: false`)

**Linting:**
- Tool: clang-tidy
- Threshold: Most checks enabled with specific exceptions
- Cognitive complexity threshold: 35 (configurable per function)
- Error threshold: all warnings treated as errors (`WarningsAsErrors: '*'`)
- Disabled checks: magic numbers, identifier length, implicit bool conversion, redundant access specifiers (see `.clang-tidy`)

**Include Organization:**
- Sorted alphabetically within groups via clang-format
- Priority order:
  1. System headers ending in `.h` (e.g., `<stdio.h>`)
  2. Modern system headers (e.g., `<cstdio>`)
  3. Third-party dependencies (nonstd, date, client, nlohmann)
  4. Private datadog headers (`datadog/c/`, `datadog/cpp/`, `datadog/impl/`)
  5. Public datadog API (`datadog.h`, `datadog.hpp`, `datadog/*`)
  6. All other headers

## Error Handling

**Patterns:**
- Use `nonstd::expected<T, ErrorType>` for operations that may fail
  - Success case: contains value accessible via `*result` or `result.value()`
  - Error case: contains error accessible via `result.error()`
  - Check success with `result.has_value()` before accessing value
  - Create error: `nonstd::make_unexpected(error_value)`
- Example from `core.cpp`:
  ```cpp
  auto clock = platform::Clock::Init();
  if (!clock) {
    return nonstd::make_unexpected(
        ErrorMessage("clock subsystem could not be initialized")
    );
  }
  ```

**Error Messages:**
- Use `ErrorMessage` type for error details
- Chain prefix information via `.AddPrefix()` method for context propagation
- Example: `filesystem_result.error().AddPrefix("event storage subsystem could not be initialized")`

**Assertions:**
- Use `DATADOG_ASSERT(condition, message)` for internal consistency checks
- Only enabled when `DD_ENABLE_ASSERTS=ON` (development mode)
- Disabled in production builds (no-op macro)
- Example from `core.cpp`:
  ```cpp
  DATADOG_ASSERT(_subsystems.storage_root, "Core created with no root storage directory");
  ```

## Logging

**Framework:** `DiagnosticLogger` (custom internal implementation)

**Patterns:**
- Create temporary logger: `impl::DiagnosticLogger{config.diagnostic_handler, config.diagnostic_threshold}`
- Methods: `.Debug()`, `.Status()`, `.Warning()`, `.Error()`
- Handler is user-configurable; default prints to stderr
- Example from `core.cpp`:
  ```cpp
  impl::DiagnosticLogger{config.diagnostic_handler, config.diagnostic_threshold}
      .Warning(
          "Events will be stored within .datadog/ in the current working directory: "
          "application should call SetEventStorageLocation to specify a suitable "
          "application-specific directory where .datadog/ can be created"
      );
  ```

## Comments

**When to Comment:**
- Comment non-obvious logic and design decisions
- Document rationale for workarounds or temporary solutions
- Use `TODO` comments sparingly; prefer JIRA-tracked issues for production code
- Current pattern allows `TODO` without JIRA ID during development

**JSDoc/Doxygen:**
- Public headers extensively documented with `/** ... */` style
- Parameter descriptions with `@param` and `@return` tags
- Example from `core.hpp`:
  ```cpp
  /**
   * Severity of a diagnostic message emitted by the SDK.
   */
  enum class DiagnosticLevel : uint8_t { Debug, Status, Warning, Error };
  ```

## Function Design

**Size:** Prefer small, focused functions
- Large test files (900+ lines) only in test context for thorough scenario coverage
- Production functions follow single-responsibility principle

**Parameters:**
- Pass by const reference for large objects
- Output parameters use `out_` prefix (e.g., `std::string& out_url`)
- Configuration objects passed by const reference

**Return Values:**
- Use `nonstd::expected<T, Error>` for operations that may fail
- Use `bool` for simple success/failure with no data
- Use `std::optional<T>` for value that may not be present
- Void for operations with no meaningful return

## Module Design

**Exports:**
- Public API in `include-cpp/datadog/` (C++ public headers)
- Public C API in `include-c/datadog/` (C public headers)
- Implementation details in `src/datadog/impl/` (private, not exposed)
- Clear separation between public and private interfaces

**Namespaces:**
- Public API: `namespace datadog { ... }`
- Implementation: `namespace datadog::impl { ... }`
- Platform-specific: `namespace datadog::platform { ... }`

**Barrel Files (Aggregation):**
- Main header `include-cpp/datadog.hpp` includes all public API headers
- Feature-specific headers: `datadog/rum.hpp`, `datadog/logging.hpp`, `datadog/core.hpp`
- Implementation detail headers: `datadog/impl/core/core.hpp`, `datadog/impl/features/rum/rum.hpp`

## Compile Flags & Build Control

**Standard versions:**
- C++ API targets: C++17 (or C++20 when crashpad enabled)
- C API targets: C99

**Common build options:**
- `DD_DEVELOPMENT=ON`: Enable all development features (tests, examples, linting)
- `DD_ENABLE_ASSERTS=ON`: Enable DATADOG_ASSERT checks
- `DD_ENABLE_CLANG_FORMAT=ON`: Enable clang-format validation
- `DD_ENABLE_CLANG_TIDY=ON`: Enable clang-tidy static analysis
- `DD_ENABLE_COVERAGE=ON`: Enable code coverage reporting
- `DD_BUILD_TESTING=ON`: Build test suite
- `DD_CRASH_MODE`: Set crash handler (noop, inprocess, or crashpad)

---

*Convention analysis: 2026-02-19*
