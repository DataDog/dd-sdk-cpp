# External Integrations

**Analysis Date:** 2026-02-19

## APIs & External Services

**Datadog Intake API:**
- **Service:** Datadog event intake endpoint
- **Purpose:** Upload telemetry events (logs, RUM, crash reports) to Datadog
- **Endpoints:** Constructed dynamically per site and feature
  - Base hosts: `browser-intake-{site}-datadoghq.com`
  - Special cases:
    - us1: `browser-intake-datadoghq.com`
    - eu1: `browser-intake-datadoghq.eu`
    - us1_fed: `browser-intake-ddog-gov.com`
- **SDK/Client:** libcurl 8.11.0
- **Auth:** DD-API-KEY header (client token supplied at configuration)
- **Protocol:** HTTPS
- **Code Location:** `src/datadog/impl/core/context.cpp`, `src/datadog/impl/core/site.hpp`, `src/datadog/impl/core/upload_thread.cpp`

## Data Storage

**Local Event Storage:**
- **Type:** Filesystem-based persistent queue
- **Location:** `.datadog/` subdirectory within configured event storage location
- **Purpose:** Buffer events for upload and survive application restarts
- **Implementation:** Batch files with TLV (Tag-Length-Value) encoding
- **Code Location:** `src/datadog/impl/core/storage_*.cpp`, `src/datadog/impl/core/tlv.cpp`

**Databases:**
- None - SDK uses filesystem-only storage

**File Storage:**
- Local filesystem (platform-specific implementations)
  - `src/datadog/impl/platform/filesystem_std.cpp` - Standard C++ filesystem
  - `src/datadog/impl/platform/filesystem_*.cpp` - Custom implementations can be supplied
- Storage location configured at runtime via `CoreConfig::SetEventStorageLocation()`

**Caching:**
- In-memory attribute caching via copy-on-write semantics (`src/datadog/impl/attribute/cow.cpp`)
- No external cache service

## Authentication & Identity

**Auth Provider:**
- Client token (Datadog application-level API key)
- Passed via `DD-API-KEY` HTTP header for intake requests

**Implementation:**
- Token supplied in `CoreConfig` at initialization
- Included in every HTTP request to intake via `HttpContext::BuildRequestHeaders()`
- No OAuth, SAML, or other external identity management

## HTTP Client Configuration

**HTTP Implementation:**
- libcurl 8.11.0 (can be bundled or system-provided)
- Supports HTTP/1.1 and HTTP/2
- TLS verification enabled by default
- Custom implementations can be supplied via `DD_HTTP_SRC` CMake option

**Request Details:**
- Content-Type: application/octet-stream or application/json (feature-specific)
- Standard headers:
  - `DD-API-KEY: {client_token}` - Authentication
  - `DD-EVP-ORIGIN: rum-cpp-sdk` - Event protocol origin
  - `DD-EVP-ORIGIN-VERSION: 4` - Event payload version
  - `DD-REQUEST-ID: {uuid}` - Request correlation
  - `User-Agent: dd-sdk-cpp/{version}` - SDK identification

**Code Location:**
- HTTP platform abstraction: `src/datadog/impl/platform/http_curl.cpp`, `include-cpp/datadog/api.hpp`
- Request building: `src/datadog/impl/core/context.cpp`
- Upload coordination: `src/datadog/impl/core/upload_thread.cpp`, `src/datadog/impl/core/upload_scheduler.cpp`

## Monitoring & Observability

**Error Tracking:**
- None - No external error tracking integration
- Errors are emitted via diagnostic logging system

**Logs:**
- Diagnostic logging API via `CoreConfig::SetDiagnosticHandler()` and `CoreConfig::SetDiagnosticThreshold()`
- Default handler: stderr output with `[DATADOG <level>]` prefix
- Supported levels: Debug, Status, Warning, Error
- Custom handler callback can be supplied for integration with application logging
- Code Location: `include-cpp/datadog/core.hpp` (DiagnosticHandler), `src/datadog/cpp/core.cpp`

## CI/CD & Deployment

**Hosting:**
- SDK is a library; deployment is via application packaging
- Supports macOS, Linux, Windows
- Generated as static or shared library artifact

**CI Pipeline:**
- GitLab CI (`.gitlab-ci.yml`)
- Stages: build-toolchain-images, validate, build, package, release
- Multi-compiler validation: clang 15, clang 20, gcc 11, gcc 13, MSVC 2022
- Test execution via ctest on all platforms
- Package generation: tar.gz (Linux/macOS), .zip (Windows)

**Build Artifacts:**
- Precompiled binaries for clang20-libstdc++-shared-release on Linux and macOS
- Windows artifacts built with MSVC v143
- Generated CMake package configs for FetchContent integration

## Webhooks & Callbacks

**Incoming:**
- None - SDK is passive event collector

**Outgoing:**
- Diagnostic handler callback: application-supplied function invoked with SDK diagnostic messages
  - Type: `using DiagnosticHandler = std::function<void(const DiagnosticMessage&)>;`
  - Code Location: `include-cpp/datadog/core.hpp`

## Tracking Consent & Privacy

**GDPR Compliance:**
- `TrackingConsent` enum: Granted, NotGranted, Pending
- Default: Pending (events stored locally but not uploaded until consent granted)
- Runtime update: `Core::SetTrackingConsent()`
- Code Location: `include-cpp/datadog/core.hpp`, `src/datadog/impl/core/core.cpp`

**Data Retention:**
- Event batches retained locally until:
  - Successfully uploaded (batch file deleted)
  - Application stops or SDK stops
  - Tracking consent revoked (all queued data cleared)

## Platform-Specific Integrations

**macOS:**
- System clock via `std::chrono` or custom implementation
- Filesystem via `std::filesystem`
- System info via CoreFoundation framework (`-framework CoreFoundation`)
- UUID generation via libc (system-provided)
- In-process crash handler uses POSIX signals

**Linux:**
- System clock via `std::chrono` or custom implementation
- Filesystem via `std::filesystem`
- System info via /proc filesystem
- UUID generation via libuuid library (`libuuid-devel` package)
- In-process crash handler uses POSIX signals

**Windows:**
- System clock via `std::chrono` or custom implementation
- Filesystem via `std::filesystem`
- System info via Windows API calls
- UUID generation via `CoCreateGuid()` from objbase.h
- In-process crash handler uses Windows vectored exception handling
- MSVC runtime library linking: static or dynamic (`/MT` or `/MD`)

## Event Format & Schemas

**Format:**
- Internal: TLV (Tag-Length-Value) binary encoding for storage batching
- Transport: JSON serialization for HTTP upload
- Validation: Can validate against rum-events-format JSON schemas via Python (development only)

**Event Types:**
- Logging: Logger events with severity levels
- RUM: View, Action, Resource, Error, Long Task events
- Crash Reports: Crash event with stack traces and system info

**Code Location:**
- JSON primitives: `src/datadog/impl/json/primitives/`
- Event serialization: Feature-specific (logging, rum, crash_reporting)
- Schema validation: `cmake/event-validation.cmake` (Python-based, development only)

---

*Integration audit: 2026-02-19*
