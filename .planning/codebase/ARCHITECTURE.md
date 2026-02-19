# Architecture

**Analysis Date:** 2026-02-19

## Pattern Overview

**Overall:** Layered architecture with clear separation between public API, feature implementations, core infrastructure, and platform abstractions. The SDK uses a multi-threaded event pipeline with dedicated storage and upload threads.

**Key Characteristics:**
- **Thin public API layer** (`src/datadog/cpp/`) that wraps implementation details
- **Feature-based modularity** with pluggable feature system (`src/datadog/impl/features/`)
- **Platform abstraction** separating interface contracts from implementations (`src/datadog/impl/platform/`)
- **Asynchronous event processing** via storage and upload background threads
- **Thread-safe event batching** with binary TLV (Tag-Length-Value) format storage
- **Scope hierarchy** for RUM events (Application → Session → View/Resource)

## Layers

**Public API Layer:**
- Purpose: Type-safe C++ and C bindings for SDK users
- Location: `include-cpp/datadog/`, `include-c/datadog/`, `src/datadog/cpp/`, `src/datadog/c/`
- Contains: Configuration classes (CoreConfig, RumConfig, LoggerConfig), feature registration interfaces, user-facing enums
- Depends on: Implementation layer (impl namespace)
- Used by: Application code, examples

**Implementation Layer (Core):**
- Purpose: Central coordination of features, platform subsystems, and event lifecycle
- Location: `src/datadog/impl/core/`
- Contains: Core class, feature registry, storage/upload scheduling, context management, batch writing
- Depends on: Platform layer, feature implementations, diagnostics
- Used by: All features, platform implementations

**Feature Implementations:**
- Purpose: Domain-specific event generation and processing (Logging, RUM, Crash Reporting)
- Location: `src/datadog/impl/features/`
- Contains: Feature-specific event types, scope hierarchies, command processing, upload report generation
- Depends on: Core Feature base class, attribute system, JSON serialization
- Used by: Core feature registry

**Platform Abstraction:**
- Purpose: Hide OS/runtime-specific implementations behind interfaces
- Location: `src/datadog/impl/platform/`
- Contains: Clock, filesystem, HTTP client, system info, crash handlers
- Depends on: External libraries (libcurl, platform SDKs), standard library
- Used by: Core infrastructure, features

**Utility Layers:**
- Purpose: Cross-cutting concerns shared by all layers
- Location: `src/datadog/impl/attribute/`, `src/datadog/impl/json/`, `src/datadog/impl/events/`
- Contains: Attribute serialization, JSON primitive encoding, event type definitions

## Data Flow

**Event Creation and Storage:**

1. **API Call** → User calls `rum->StartView()`, `logger->Info()`, etc.
2. **Feature Processing** → Feature validates input, creates event command (e.g., `RumCommand`)
3. **Event Writing** → Feature calls `Feature::WriteEvent()` with binary TLV-encoded payload
4. **Queue Dispatch** → Event is queued in thread-safe queue (`src/datadog/impl/core/queue.hpp`)
5. **Storage Thread** → Reads from queue, deserializes, applies attributes, writes to batch file
6. **Batch Management** → BatchWriter manages file rotation based on size/age constraints

**Event Upload:**

1. **Upload Scheduler** → Periodically triggers upload cycles based on configured frequency
2. **Batch Discovery** → Upload thread scans feature directories for ready batches
3. **Report Generation** → Feature's `UploadThread_PrepareReport()` assembles HTTP request (URL, headers, body)
4. **HTTP Request** → Platform HTTP client executes request to Datadog intake
5. **Cleanup** → Successful uploads trigger batch file deletion; failures trigger retry scheduling

**State Management:**

- **Global Attributes** → Stored in feature (e.g., RUM global attributes), merged into events during storage
- **RUM Scope Hierarchy** → Application scope manages session scopes; sessions manage view scopes; views track resources/actions
- **Tracking Consent** → Controls whether events are written to "pending" vs "granted" directories; conversion on consent change
- **Session Lifecycle** → Sessions auto-expire after 15 minutes inactivity or 4 hours total; explicit stop via API

## Key Abstractions

**Feature:**
- Purpose: Base class defining contract for event-generating components
- Examples: `src/datadog/impl/features/rum/rum.hpp`, `src/datadog/impl/features/logging/logging.hpp`
- Pattern: Subclasses implement `GetId()`, `GetName()`, `Start()`, `Stop()`, `UploadThread_PrepareReport()`; call `WriteEvent()` to emit events

**RUM Scope Hierarchy:**
- Purpose: Model application state as nested scopes with command-driven state transitions
- Examples: `src/datadog/impl/features/rum/scopes/application.hpp`, `session.hpp`, `view.hpp`
- Pattern: Each scope processes commands (SDKInit, StartView, AddAction, etc.) and returns `RumScopeResult` (Process/Close); scopes contain attributes that merge into events

**BatchWriter:**
- Purpose: Manage persistent batch file lifecycle (creation, rotation, cleanup)
- Location: `src/datadog/impl/core/storage_write.hpp`
- Pattern: Tracks current file, decides when to rotate based on age/size; writes TLV blocks to files; respects consent state

**FeatureScope (Command Queue):**
- Purpose: Provide thread-safe command dispatch to feature-specific processing
- Location: `src/datadog/impl/core/feature_scope.hpp`
- Pattern: Features call `WriteEvent()` from main thread; commands are queued; storage thread dequeues and processes asynchronously

## Entry Points

**Core Initialization:**
- Location: `include-cpp/datadog/core.hpp`, `src/datadog/cpp/core.cpp`
- Triggers: `Core::Create(config)` call from application
- Responsibilities: Construct Core, initialize subsystems (clock, storage, HTTP), return shared_ptr to user

**Core Start:**
- Location: `src/datadog/impl/core/core.hpp::Core::Start()`
- Triggers: `core->Start()` after features registered
- Responsibilities: Spawn storage and upload threads, begin processing queued events

**Feature Registration:**
- Location: `include-cpp/datadog/rum.hpp::Rum::Register()`, `logging.hpp::Logging::Register()`
- Triggers: Feature-specific registration call (e.g., `Rum::Register(core, config)`)
- Responsibilities: Construct feature, register with core, return shared_ptr to user for public API calls

**API Methods:**
- Location: Feature-specific headers (rum.hpp, logging.hpp)
- Triggers: User calls methods like `rum->StartView()`, `logger->Info()`
- Responsibilities: Validate inputs, dispatch commands to feature, may call `WriteEvent()` immediately or queue for deferred processing

## Error Handling

**Strategy:** Errors are logged via diagnostic callbacks; non-critical failures do not prevent SDK operation; data loss is preferred over crashes.

**Patterns:**
- **Initialization errors** → Return error via `nonstd::expected<T, ErrorMessage>` pattern; Core/features fail gracefully
- **I/O errors** → Logged diagnostically; failed writes are retried or discarded based on feature
- **Validation errors** → Logged as warnings; invalid inputs are dropped (e.g., blank action names)
- **Thread errors** → Storage/upload thread panics are caught and logged; SDK continues without that thread

## Cross-Cutting Concerns

**Logging:** DiagnosticLogger callback system (`src/datadog/impl/diagnostics.hpp`); threshold-based filtering; thread-safe invocation

**Validation:** Input validation at API entry points (e.g., RumConfig constructor validates UUID); core validates feature IDs to prevent collisions

**Authentication:** Client token and site configuration at Core level; passed to HTTP client via context

**Attribute Merging:** Copy-on-write attribute maps (`src/datadog/impl/attribute/cow.hpp`); merge order: global < view < command

**Concurrency:** Storage thread reads from event queue; upload thread reads from filesystem; no shared state except through synchronized core context
