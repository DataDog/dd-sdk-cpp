# Codebase Concerns

**Analysis Date:** 2026-02-19

## Tech Debt

**Crash Handler Initialization and Lifecycle (RUM-14021, RUM-14025):**
- Issue: Unclear guarantees about how many times ICrashHandler::Initialize() will be called and whether it's reversible across multiple SDK instances in the same process
- Files: `src/datadog/impl/platform/crash_handler.hpp` (lines 80-86, 96-100), `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp` (line 791), `src/datadog/impl/platform/crash_handler_crashpad.cpp` (line 109)
- Impact: Multiple SDK instances with crash reporting enabled may interfere with each other if handlers aren't process-wide; unclear whether Shutdown() is reliably reversible
- Fix approach: Document explicit guarantees about process-wide state and whether Initialize()/Shutdown() cycles are supported; add explicit checks/guards for multiple SDK instances

**Feature Modularization Design (RUM-11669):**
- Issue: Crash handler selection via CMake compile-time flag (DD_CRASH_MODE) is decided before feature modularization plan is finalized; unclear where crash handler belongs if features become modular
- Files: `src/datadog/impl/platform/crash_handler.hpp` (lines 45, 50-52)
- Impact: Future refactoring to modularize features may require significant rework of crash handler architecture
- Fix approach: Clarify whether crash handler will remain in `datadog::platform` or move to feature-specific module; update architecture decisions in design docs

**String View Lifetime Hazard (RUM-13165):**
- Issue: RUM command payloads store `std::string_view` members (keys, names, URLs) that may reference temporary strings; when command processing moves to separate thread, these dangling references will cause crashes
- Files: `src/datadog/impl/features/rum/command.hpp` (lines 120-121, 135, 152, 165, 178-180, 181)
- Impact: Current single-threaded command processing is safe; future thread-based processing will have memory safety violations
- Fix approach: Replace `std::string_view` with `std::string` in all RUM command payload types before threading command processing (RUM-13164 and RUM-13165 are dependent)

**Crashpad Database Location Hardcoded (RUM-14020):**
- Issue: Crashpad minidump database stored in hardcoded `.crashpad/` directory in current working directory instead of SDK storage directory; no integration with SDK-level storage root
- Files: `src/datadog/impl/platform/crash_handler_crashpad.cpp` (line 87)
- Impact: Crash artifacts accumulate in application's working directory; not cleaned up with SDK storage; may fill disk on long-running processes
- Fix approach: Move crashes to SDK-managed storage directory (configured via CoreConfig); implement cleanup policy aligned with other SDK data

**Crash Report Directory Location Hardcoded (WIP):**
- Issue: In-process crash handler stores crash reports in hardcoded `.crashes/` directory instead of SDK storage root
- Files: `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp` (line 794), `src/datadog/impl/platform/crash_handler_inprocess_windows.cpp` (line 459)
- Impact: Crash reports not cleaned up with other SDK data; working directory pollution
- Fix approach: Make crash storage location configurable as part of SDK storage root

**Source Type Hardcoded to 'unity' (RUM-7416):**
- Issue: Event context source value hardcoded to "unity" instead of "rum-cpp" because backend doesn't support cpp-specific source type
- Files: `src/datadog/impl/core/context.hpp` (line 56), `src/datadog/impl/core/context.cpp` (line 27)
- Impact: RUM events mislabeled as Unity SDK instead of C++ SDK; affects backend analytics and filtering
- Fix approach: Coordinate with backend team to add "rum-cpp" as supported source type; update context.cpp to use correct source

**User-Agent Header Hardcoded (WIP):**
- Issue: HTTP requests use placeholder User-Agent string "nobody" instead of SDK-specific identifier
- Files: `src/datadog/impl/core/context.cpp` (line 118)
- Impact: Server cannot identify SDK version from requests; affects debugging and analytics
- Fix approach: Generate proper User-Agent with SDK name and version; include in all HTTP requests

**HTTP Header Format Inconsistency (WIP):**
- Issue: Comments indicate wire format should use `\r\n` delimiters, but application currently expects non-standard `\n`; curl automatically handles this but discrepancy creates confusion
- Files: `src/datadog/impl/platform/http.hpp` (lines 100-102)
- Impact: Potential incompatibility if HTTP client implementation changes; confusing documentation
- Fix approach: Clarify and document expected header format consistently; update curl implementation if needed to use standard `\r\n`

**Batch File Retry Logic Inefficient (no ticket):**
- Issue: If batch file deletion fails after successful upload, file will be continuously re-uploaded on every cycle until deletion succeeds; no tracking of processed files
- Files: `src/datadog/impl/core/upload_thread.cpp` (lines 258-263, 318-323)
- Impact: Network bandwidth waste and increased server load if a batch file cannot be deleted (e.g., permission issue)
- Fix approach: Maintain persistent record of processed batches; skip re-uploading already-processed batches even if deletion fails

**HTTP Header List Reallocation Per Request (WIP):**
- Issue: Curl header linked list (`curl_slist*`) rebuilt on every HTTP request instead of cached; headers rarely change between requests
- Files: `src/datadog/impl/platform/http_curl.cpp` (lines 133-135, 224)
- Impact: Unnecessary allocations and string copying on every upload cycle
- Fix approach: Cache small set of curl_slist* structures indexed by common header sets; reuse across requests

**RUM Event Filtering Incomplete (RUM-12546):**
- Issue: View events streamed directly without deduplication/filtering; architecture assumes `view_update` events will be implemented later to make filtering unnecessary
- Files: `src/datadog/impl/features/rum/rum.cpp` (lines 28-32)
- Impact: Redundant view events sent to backend; potential duplicate data
- Fix approach: Evaluate whether view_update events will be implemented; if not, add filtering logic for view events

## Known Bugs

**Alternate Signal Stack Per-Thread Limitation (WIP):**
- Symptoms: Crash handler can only reliably capture crashes on main thread if stack exhaustion is involved; other threads may not have alternate signal stack configured
- Files: `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp` (lines 838-842)
- Trigger: App with many threads crashes due to stack overflow on non-main thread
- Workaround: Currently no workaround; crashes on non-main threads without sufficient stack space may not be captured
- Fix approach: Expose public API for applications to register thread-local signal stacks; document current limitation in crash handler API

**Signal Stack Not Restored Per-Thread (WIP):**
- Symptoms: Shutdown does not restore previous alternate stack configuration
- Files: `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp` (lines 929-930)
- Trigger: Shutdown followed by re-Initialize in same process
- Workaround: Application should ensure only one crash handler Initialize/Shutdown cycle per process
- Fix approach: Save and restore previous stack_t in Shutdown(); ensure idempotent behavior

**Reentrancy Guard One-Shot Only:**
- Symptoms: First crash in process is captured; if handler crashes internally, second crash uses OS default handler instead of continuing with custom handler
- Files: `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp` (line 72)
- Trigger: Recursive signal (e.g., handler dereferences invalid pointer)
- Workaround: Ensure signal handler only calls async-signal-safe functions; avoid undefined behavior during crash capture
- Fix approach: Document this limitation; consider implementing signal handler state machine to support limited retry

**Crashpad Uploads Disabled (RUM-14025):**
- Symptoms: Crashpad handler never uploads minidumps to backend; files accumulate in local database
- Files: `src/datadog/impl/platform/crash_handler_crashpad.cpp` (line 120)
- Trigger: Enable crash reporting with `DD_CRASH_MODE=crashpad`
- Workaround: None; must use inprocess handler instead
- Fix approach: Re-enable Crashpad upload support; implement backend endpoint to accept Breakpad minidump format with RUM context

## Security Considerations

**Async-Signal-Safety Constraints (WIP):**
- Risk: Crash handler uses only async-signal-safe functions to avoid undefined behavior in signal handler context; however, some dependencies (malloc, logging, etc.) may not be fully signal-safe
- Files: `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp` (line 51, throughout)
- Current mitigation: Comments document async-signal-safety constraints; assert before Initialize() that we're in safe state; only use approved functions
- Recommendations: Audit all function calls in crash_signal_handler() for signal-safety; consider static analysis to prevent unsafe functions being added; document approved function list

**Pre-Opened File Descriptor Management:**
- Risk: Crash handler maintains static global file descriptor (s_crash_fd) opened indefinitely; descriptor leaked if shutdown fails; could cause file descriptor exhaustion
- Files: `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp` (line 67-68)
- Current mitigation: Descriptor closed in Shutdown(); Initialize() validates it was closed before re-opening
- Recommendations: Add cleanup finalizer; ensure Shutdown() is called via RAII where possible; add asserts to verify descriptor state

**Signal Mask Inherited by Handler:**
- Risk: Signal handler doesn't explicitly control signal mask; could inherit dangerous mask configuration from application
- Files: `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp` (line 864)
- Current mitigation: Handler uses SA_RESTART flag to restart interrupted syscalls
- Recommendations: Document signal masking behavior; consider explicitly unmasking critical signals in handler if needed

## Performance Bottlenecks

**RUM Command Dispatch Synchronous:**
- Problem: All RUM commands processed synchronously on caller's thread (logging, user interaction, network events); command queue could block application threads
- Files: `src/datadog/impl/features/rum/rum.cpp`, `src/datadog/impl/features/rum/scopes/` (throughout)
- Cause: Commands not yet moved to separate thread (RUM-13164 is planned)
- Improvement path: Implement async command processing thread; use thread-safe queue for command dispatch (RUM-13164)

**HTTP Request Headers Rebuilt Every Upload:**
- Problem: Curl header linked list rebuilt on every HTTP request; wasted allocations for headers that rarely change
- Files: `src/datadog/impl/platform/http_curl.cpp` (lines 133-139)
- Cause: No caching of header structures
- Improvement path: Implement small LRU cache of curl_slist* objects indexed by request headers; measure cache hit rate in typical scenarios

**Timestamp Synchronization Latency:**
- Problem: Every RUM command and event includes timestamp; no clock optimization or batching of timestamp queries
- Files: `src/datadog/impl/platform/clock.hpp` (throughout)
- Cause: Each timestamp call queries system clock independently
- Improvement path: Measure clock query overhead; consider caching timestamp for batch of commands; evaluate `clock_gettime(CLOCK_MONOTONIC)` vs other clocks

## Fragile Areas

**RUM Scope Hierarchy (Session → View → Action/Resource):**
- Files: `src/datadog/impl/features/rum/scopes/application.cpp`, `src/datadog/impl/features/rum/scopes/session.cpp`, `src/datadog/impl/features/rum/scopes/view.cpp`
- Why fragile: Complex state transitions and command propagation logic; views can be inactive but remain open (waiting for resources); actions have auto-stop timers; resources can extend action lifetime. Changes to session/view lifecycle easily break child scope assumptions.
- Safe modification: Document exact state transition invariants before changing; add assertions for expected states at scope boundaries; expand test coverage for edge cases (nested view starts, rapid stop/start sequences, resource completion timing)
- Test coverage: View/Action/Resource lifecycle partially tested; missing: view refresh during resource in-flight, off-view command handling, background view creation

**Application Launch View Creation (RUM-12242, RUM-12243, RUM-12245, RUM-12246, RUM-12247):**
- Files: `src/datadog/impl/features/rum/scopes/application.cpp` (lines 37-40, 131-133, 162-164, 204-206), `src/datadog/impl/features/rum/scopes/session.cpp` (lines 123-124, 148, 151), `src/datadog/impl/features/rum/scopes/view.cpp` (line 125-128, 303-305), `src/datadog/impl/features/rum/rum.cpp` (lines 31-32)
- Why fragile: Multiple incomplete TODOs around ApplicationLaunch view creation, synthetic Background view, and view transfer on session refresh. Current implementation assumes foreground launch; background launch not handled. Several TODOs indicate incomplete feature design.
- Safe modification: Complete and document launch detection design before implementing view creation logic; add configuration for launch mode (foreground vs background); add comprehensive tests for launch scenarios
- Test coverage: ApplicationLaunch view creation not yet implemented or tested

**Upload Thread State and Timing:**
- Files: `src/datadog/impl/core/upload_thread.cpp` (throughout)
- Why fragile: Adaptive backoff logic, file age checking, batch retry logic, and feature-specific scheduling interact in complex ways. Timing bugs easily cause missed uploads or excessive retries.
- Safe modification: Understand adaptive backoff algorithm before changing timing; add unit tests for upload cycle scheduling; mock clock for deterministic testing; verify no upload cycles are skipped due to timing races
- Test coverage: Upload timing and backoff partially tested; missing: clock drift scenarios, rapidly alternating online/offline states, batch deletion failure recovery

**Crash Handler Signal Installation and Chaining:**
- Files: `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp` (lines 856-900)
- Why fragile: Installs handlers for 5 different signals with complex cleanup on partial failure. Handler must chain to previous handlers if re-entrant crash occurs. One-shot reentrancy guard prevents second crash handling.
- Safe modification: Add detailed comments documenting signal installation order and rollback logic; verify cleanup is truly symmetric; test partial failures in handler installation; validate chaining works with known signal handlers (e.g., sanitizers)
- Test coverage: Signal handler installation partially tested; missing: partial installation failure scenarios, chaining to non-standard previous handlers, signal interaction with application's own handlers

## Scaling Limits

**In-Process Crash Handler Stack Limit:**
- Current capacity: 128 KiB alternate signal stack allocated per process
- Limit: Very deep call stacks (>1000 frames) or large stack allocation in handler will exhaust alternate stack; stack overflow in handler itself may not be captured
- Scaling path: Profile actual stack usage during crash capture; increase if needed; consider lazy allocation of larger stacks; document maximum safe stack depth

**Crash Report File Size:**
- Current capacity: Stack trace written to disk with max 256 stack frames per crash; file descriptor kept open indefinitely
- Limit: Large number of crashes in process lifetime could accumulate; no built-in rotation or cleanup
- Scaling path: Implement crash report rotation (max N files, delete oldest); integrate with SDK storage cleanup; set reasonable file size limits

**RUM View/Action/Resource Count:**
- Current capacity: No explicit limits on concurrent views, actions, or resources per session
- Limit: Long-running apps with many views/resources could accumulate state; session objects grow unbounded
- Scaling path: Measure actual resource usage in typical apps; implement reasonable limits (e.g., max 100 concurrent views); add diagnostic logging for resource exhaustion

**HTTP Batch Size and Upload Frequency:**
- Current capacity: Configurable batch size and upload frequency; default to reasonable values but not tuned for high-traffic scenarios
- Limit: Very high event volume could cause upload thread to fall behind; batch files accumulate on disk
- Scaling path: Implement adaptive batch sizing based on event volume and network conditions; monitor upload thread lag; consider multi-threaded uploads for high-volume scenarios

## Dependencies at Risk

**Libcurl HTTP Client:**
- Risk: Critical dependency for all event uploads; requires libcurl 7.x; custom header handling and chunked encoding implementation
- Impact: Curl bugs or incompatibilities break event delivery; potential security issues in older libcurl versions
- Migration plan: Document minimum libcurl version requirements; consider abstraction layer to support alternative HTTP clients (e.g., system-level HTTP on Windows/macOS); keep curl code isolated in `http_curl.cpp`

**CMake Build System:**
- Risk: Complex CMake configuration with many optional dependencies (libcurl, Catch2, clang-tidy, clang-format); version requirements (3.21+)
- Impact: Build failures on systems with older CMake or missing tools; difficult to modify build without deep CMake knowledge
- Migration plan: Document all CMake requirements and optional tools clearly; consider migration to modern build tools (Bazel, meson) if project scales; provide pre-built binaries to reduce build burden

**POSIX Signal API (Platform-Specific):**
- Risk: Crash handler heavily dependent on POSIX signals (sigaction, sigaltstack); only works on POSIX systems (Linux, macOS); Windows implementation completely different
- Impact: Crash handler implementation must be completely forked for Windows; bugs fixed in one platform must be fixed separately in other; security issues in signal handling hard to test cross-platform
- Migration plan: Abstracted crash handler interface (ICrashHandler) allows platform-specific implementations; ensure parity tests verify behavior matches across platforms

## Missing Critical Features

**ApplicationStart Command (RUM-12242):**
- Problem: RUM spec includes ApplicationStart command to handle foreground launch of app; not yet implemented
- Blocks: Correct session initialization and ApplicationLaunch view creation on foreground app launch
- Implementation status: API interface defined but handler not implemented; multiple TODOs in scope code

**Synthetic Background View (RUM-11247, RUM-12247):**
- Problem: Off-view RUM commands should create synthetic Background view; not yet implemented
- Blocks: Accurate RUM event collection for background app execution
- Implementation status: Not started; design incomplete

**Resource Metrics (RUM-13166):**
- Problem: Detailed RUM resource metrics (size, timing breakdown) require AddResourceMetrics command; not yet implemented
- Blocks: Full resource performance visibility
- Implementation status: API placeholder; command handler missing

**View Update Events (RUM-12546):**
- Problem: Current implementation streams all view events without deduplication; view_update events would enable efficient deduplication
- Blocks: Reducing duplicate view events; backend filtering logic
- Implementation status: Not started; design unclear

**Trace Context Injection (RUM-13167):**
- Problem: Resource scope should inject distributed trace context for correlation with APM traces
- Blocks: RUM/Trace correlation for resource operations
- Implementation status: Not started

## Test Coverage Gaps

**Crash Handler Edge Cases:**
- What's not tested: Partial signal handler installation failures, crash during crash handler setup, recursive crashes, signal masking edge cases
- Files: `src/datadog/impl/platform/crash_handler_inprocess_posix.cpp`, `src/datadog/impl/platform/crash_handler_inprocess_windows.cpp`
- Risk: Critical bugs in crash handling discovery only in production when applications actually crash
- Priority: High

**RUM Scope State Machine:**
- What's not tested: View refresh during resource in-flight, off-view commands, background view creation, ApplicationStart handling, rapid view/action transitions
- Files: `src/datadog/impl/features/rum/scopes/` (throughout)
- Risk: State corruption under concurrent user actions and network events
- Priority: High

**Upload Thread Retry Logic:**
- What's not tested: Batch deletion failures and retry behavior, adaptive backoff state, rapid online/offline transitions, network timeout scenarios
- Files: `src/datadog/impl/core/upload_thread.cpp`
- Risk: Events lost or infinitely retried under certain failure conditions
- Priority: High

**Memory Safety with String Views:**
- What's not tested: Dangling string_view references if RUM commands are queued to separate thread (future: RUM-13164)
- Files: `src/datadog/impl/features/rum/command.hpp`
- Risk: Crashes due to use-after-free when command processing is threaded
- Priority: Critical (blocks RUM-13164)

**Platform-Specific HTTP Implementation:**
- What's not tested: Chunked encoding correctness, header format edge cases, large payload handling, connection timeout scenarios
- Files: `src/datadog/impl/platform/http_curl.cpp`
- Risk: Incomplete or malformed uploads; undetected connection errors
- Priority: Medium

---

*Concerns audit: 2026-02-19*
