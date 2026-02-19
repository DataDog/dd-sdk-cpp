# Testing Patterns

**Analysis Date:** 2026-02-19

## Test Framework

**Runner:**
- Catch2 (latest version via FetchContent)
- Config: No separate config file; integrated via CMake

**Assertion Library:**
- Catch2 macros: `REQUIRE`, `REQUIRE_FALSE`, `CHECK`

**Run Commands:**
```bash
cmake -S . -B build -DDD_DEVELOPMENT=ON
cmake --build build
ctest --test-dir build                   # Run all tests
ctest --test-dir build -V                # Run all tests (verbose)
ctest --test-dir build -R pattern        # Run tests matching pattern
```

## Test File Organization

**Location:**
- Co-located with implementation: test files live in `tests/` directory mirroring source structure
- `tests/impl/` mirrors `src/datadog/impl/`
- `tests/cpp/` for C++ public API tests
- `tests/c/` for C public API tests
- `tests/support/` for test utilities and mocks
- `tests/mock/` for mock implementations

**Naming:**
- `{module}_test.cpp` pattern (e.g., `core_test.cpp`, `rum_test.cpp`)
- Test discovery via Catch2 with automatic registration

**Structure:**
```
tests/
├── impl/                          # Implementation detail tests
│   ├── core/
│   │   ├── core_test.cpp
│   │   ├── storage_write_test.cpp
│   │   ├── upload_thread_test.cpp
│   │   └── ...
│   ├── features/
│   │   ├── rum/
│   │   │   └── rum_test.cpp
│   │   └── logging/
│   │       └── logger_test.cpp
│   └── ...
├── cpp/                           # C++ public API tests
│   ├── core_cpp_api_test.cpp
│   ├── rum_cpp_api_test.cpp
│   └── ...
├── c/                             # C public API tests
│   ├── core_c_api_test.cpp
│   ├── rum_c_api_test.cpp
│   └── ...
├── mock/                          # Mock implementations
│   ├── clock.hpp
│   ├── feature.hpp
│   ├── filesystem.hpp
│   └── ...
└── support/                       # Test utilities
    ├── core.hpp
    ├── feature.hpp
    └── ...
```

## Test Structure

**Suite Organization:**
```cpp
using namespace datadog;
using namespace datadog::impl;

TEST_CASE("Core Lifecycle", "[unit]") {
  SECTION("M create Core in Uninitialized state W constructor called") {
    // Given: setup phase
    impl::Core core = _make_core();

    // When: action phase
    // (action is implicit in the name or setup)

    // Then: assertion phase
    REQUIRE(core.Init());
  }

  SECTION("M transition to Initialized state W Init called on Uninitialized core") {
    // Given an uninitialized core
    impl::Core core = _make_core();

    // When Init() is called
    bool result = core.Init();

    // Then Init should succeed
    REQUIRE(result);
  }
}
```

**Patterns:**
- `TEST_CASE` for top-level test grouping (one per file, usually matches filename)
- `SECTION` for individual test scenarios (one assertion per section or tightly related)
- Section naming: "M [expected behavior] W [given condition]" (e.g., "M create Core in Uninitialized state W constructor called")
- Three-phase structure: Given (setup), When (action), Then (assertions)
- Comments document the flow for readability

**Common Fixtures:**
- `CoreTestHarness`: Encapsulates full Core setup with mocks for filesystem, HTTP, clock, system info
  - Located in `tests/support/core.hpp`
  - Provides `Init()` static factory method
  - Manages lifecycle and captures diagnostics
- `FeatureTest`: Test wrapper for feature-level testing with context management
  - Located in `tests/support/feature.hpp`
  - Methods: `Start(feature)`, `Stop(feature)`, `GetContextSync()`
- `MockClock`: Deterministic time control for testing
  - Methods: `FreezeAtMilliseconds()`, `Tick(duration)`
- `MockStorageDirectory`: In-memory filesystem for storage testing
  - Methods: `PrepareSubdirectory()`, `FindFiles()`, `Cat()`
- `MockHttpClient`: Records HTTP requests for inspection
- `MockFeature`: Base mock feature implementation for testing Core

## Mocking

**Framework:** Custom mock implementations (not gmock)

**Patterns:**
```cpp
// Example from tests/impl/core/core_test.cpp
static impl::Core _make_core() {
  return impl::Core(
      CoreConfig("test-client-token", "initial-service", "initial-env")
          .SetInitialTrackingConsent(TrackingConsent::Granted)
          .SetApplicationVersion("1.0.0")
          .SetBatchSize(BatchSize::Small)
          .SetUploadFrequency(UploadFrequency::Frequent)
          .SetBatchProcessingLevel(BatchProcessingLevel::Low),
      CoreSubsystems(
          std::make_unique<MockClock>(),
          std::make_unique<MockStorageDirectory>(),
          std::make_unique<MockHttpSubsystem>(),
          std::make_unique<MockSystemInfo>()
      )
  );
}

// Example from tests/mock/feature.hpp
class MockFeature : public impl::Feature {
 public:
  impl::FeatureId id;
  std::string name;
  std::string path{"/api/v1/test"};
  std::string content_type{"text/plain"};
  std::string feature_headers{""};
  int num_start_calls{0};
  int num_stop_calls{0};
  std::vector<MockReport> reports;

  MockFeature(impl::FeatureId in_id, std::string_view in_name)
      : id(in_id), name(in_name) {}

  impl::FeatureId GetId() const override { return id; }
  std::string_view GetName() const override { return name; }
  void Start() override { num_start_calls++; }
  void Stop() override { num_stop_calls++; }
};
```

**What to Mock:**
- External dependencies: Clock, Filesystem, HTTP client, System info
- Features when testing Core's feature management
- Test fixtures that enable deterministic testing

**What NOT to Mock:**
- Core logic components under test
- Actual event generation and serialization
- Data structures representing business logic
- Always test the real implementation of the feature being tested

## Fixtures and Factories

**Test Data:**
```cpp
// From tests/impl/features/rum/rum_test.cpp
static const UUID APPLICATION_ID = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
static const CoreConfig CORE_CONFIG("mock-client-token", "mock-service", "mock-env");
static const RumConfig RUM_CONFIG(APPLICATION_ID);
static const platform::OsInfo OS_INFO{
    "mock-os", "2.3.4", "mock-build-number", "2"
};
static const platform::DeviceInfo DEVICE_INFO{
    "desktop", "mock-device", "mock-model", "mock-brand",
    "x86_64", "en-US", "America/New_York"
};

// Factory function pattern
static impl::Core _make_core() {
  return impl::Core(
      CoreConfig("test-client-token", "initial-service", "initial-env")
          .SetInitialTrackingConsent(TrackingConsent::Granted)
          .SetApplicationVersion("1.0.0"),
      CoreSubsystems(
          std::make_unique<MockClock>(),
          std::make_unique<MockStorageDirectory>(),
          std::make_unique<MockHttpSubsystem>(),
          std::make_unique<MockSystemInfo>()
      )
  );
}
```

**Location:**
- Static const fixtures defined at file scope in test files
- Factory functions as file-scoped static functions
- Shared test utilities in `tests/support/` and `tests/mock/`

## Coverage

**Requirements:** Not enforced by default; optional via `DD_ENABLE_COVERAGE=ON`

**View Coverage:**
```bash
cmake --build build --target dd_native_tests_coverage
open build/coverage/index.html  # macOS
```

## Test Types

**Unit Tests:**
- Scope: Individual components in isolation (Core, Feature, storage, JSON encoding)
- Approach: Mock external dependencies, test single responsibility
- Location: `tests/impl/`, `tests/json/`, `tests/attribute/`, `tests/features/`
- Example: `tests/impl/core/core_test.cpp` - tests Core lifecycle without HTTP/filesystem operations

**Integration Tests:**
- Scope: Multiple components working together with real implementations
- Approach: Use CoreTestHarness with mock filesystem/HTTP but real serialization logic
- Location: `tests/impl/core/` (e.g., `storage_write_test.cpp`, `upload_thread_test.cpp`)
- Example: `tests/impl/core/storage_write_test.cpp` - tests BatchWriter event serialization and file creation

**API Tests:**
- Scope: Public C++ and C API surface correctness
- Approach: Test both public APIs work correctly with real implementation
- Location: `tests/cpp/` and `tests/c/`

**E2E Tests:**
- Not used in unit test suite
- Integration testing available via `DD_ENABLE_INTEGRATION_TEST` with Python-based tests

## Common Patterns

**Async Testing:**
```cpp
// From tests/support/core.hpp - handling async storage/upload threads
MockClock clock;
MockStorageDirectory storage;
clock.FreezeAtMilliseconds(1700000000000);  // Freeze time for deterministic testing
// ... trigger async operations ...
// Results are captured in mock storage/HTTP client for inspection
```

**Error Testing:**
```cpp
// Test error conditions using expected<T, Error>
auto result = platform::Filesystem::Init(path);
REQUIRE_FALSE(result.has_value());
REQUIRE(result.error() == FilesystemError::NotFound);

// Test error propagation
if (!clock) {
  return nonstd::make_unexpected(
      ErrorMessage("clock subsystem could not be initialized")
  );
}
```

**Context Testing:**
```cpp
// From tests/impl/features/rum/rum_test.cpp
CoreContext context = test.GetContextSync();
REQUIRE(context.rum);
REQUIRE(context.rum->application_id == APPLICATION_ID);
REQUIRE(context.rum->session_id != UUID::Zero);
```

**Event Verification:**
```cpp
// From tests/impl/features/logging/logger_test.cpp
REQUIRE(test.events.size() == 1);
const CapturedEvent& event = test.events.back();
REQUIRE(has_uuid_value(event.data, "application_id", uuid_9916));
REQUIRE(has_uuid_value(event.data, "session_id", uuid_d927));
```

## Test Configuration

**Memory tracking:** Enabled by default via `DD_ENABLE_TEST_ALLOCATION_TRACKING=ON`
- Allows detection of memory leaks and allocation patterns
- Can be disabled if allocation instrumentation interferes with testing

**Strict memory checks:** Platform-dependent; enabled by default on macOS/Clang/libc++
- Requires `DD_ENABLE_STRICT_MEMORY_CHECKS=ON`

**Thread safety checks:** Disabled by default (timing-dependent, flaky)
- Can be explicitly enabled with `DD_ENABLE_STRICT_THREADING_CHECKS=ON`

## Building Tests

```bash
# Standard development build includes tests
cmake -S . -B build -DDD_DEVELOPMENT=ON
cmake --build build

# Run all tests with CTest
ctest --test-dir build --output-on-failure

# Run specific test
ctest --test-dir build -R "Core Lifecycle" -V

# Run with sanitizers (enabled by default in development)
# Address Sanitizer, Undefined Behavior Sanitizer, etc.
```

---

*Testing analysis: 2026-02-19*
