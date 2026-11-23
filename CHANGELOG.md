## 0.10.0

### Breaking Changes

- The C API config types (`dd_core_config_t`, `dd_rum_config_t`, `dd_logger_config_t`, `dd_crash_reporting_config_t`, `dd_profiling_config_t`) are now opaque handles: callers must use the new `dd_*_config_create()` / `dd_*_config_destroy()` functions instead of stack-allocating structs and calling `dd_*_config_init()`.
- `EventWriter` now exposes `WriteMove()` for zero-copy submission of event bytes and `WriteCopy()` for the previous copy-based behavior; any existing calls to the old write method must be updated to use one of these two replacements.

### Features

- Distributed tracing is now supported: configure first-party hosts with `FirstPartyHosts` (a list of `FirstPartyHost` entries pairing a hostname with its trace header types) and inject trace context into outgoing HTTP requests via `HttpHeaderWriter.CreateHeaders()`, with support for Datadog, B3 single-header, B3 multi-header, and W3C propagation formats.
- Individual SDK features (`DD_FEATURE_LOGGING`, `DD_FEATURE_RUM`, `DD_FEATURE_CRASH_REPORTING`, `DD_FEATURE_PROFILING`) can now be disabled at CMake configure-time to produce a smaller library that omits the corresponding APIs and implementation code.
- Windows CPU profiling data is now collected and delivered through the native C callback interface, enabling profiling support on Windows.
- The context-changed callback now includes the current user ID and account ID, providing empty strings when those values have not been set.
- `DD_SITE_UK1` and `datadog::Site::uk1` are now supported; when configured, the SDK uploads data to `https://browser-intake-uk1-datadoghq.com`.

### Fixes

- Upload scheduling delays now match the iOS and Android SDKs: the minimum, initial, and maximum intervals for each `UploadFrequency` level (`Frequent`, `Average`, `Rare`) are corrected to 0.5s/2.5s/5.0s, 2.0s/10.0s/20.0s, and 5.0s/25.0s/50.0s respectively, replacing the previously too-large values.
- Subnormal `double` values (e.g. `5e-324`) now parse correctly on platforms that use the `strtod_l` fallback (macOS, iOS, and Android with older NDKs), fixing a round-trip bug where such values written by the SDK could not be read back.

## 0.9.0

### Fixes

- Linux builds no longer require `libuuid`; UUID generation now uses `getrandom()` directly with a `/dev/urandom`-based fallback, requiring glibc 2.25 or newer.
- Linux shared-library builds with `DD_CRASH_MODE=crashpad` no longer fail due to the CMake `LINKER:` prefix being passed through to Clang/GN unchanged.
- We now publish a static-library release artifact for Linux that's built with `clang/libc++` targeting a glibc 2.28 sysroot.

## 0.8.1

### Features

- We now publish precompiled releases of the SDK as a static library, in addition to the existing shared-library builds.
- Shared-library release artifacts now include separate debug-info files on all platforms: `.pdb` on Windows, `.dSYM` on macOS, and a stripped `.so` with an accompanying `.so.debug` on Linux.
- Release archives are now packaged as `.zip` file on all platforms. If your build process previously consumed binary releases of `dd-sdk-cpp` from GitHub for macOS or Linux, note that these files are now `.zip` instead of `.tar.gz`.

### Fixes

- For Windows builds, `CMAKE_MSVC_RUNTIME_LIBRARY` is now correctly documented as controlling CRT linkage flavor. Previously, comments in `CMakeLists.txt` advised setting a variable called `MSVC_RUNTIME_LIBRARY`, which is incorrect.
- Linux builds now correctly require `libuuid` by name rather than embedding the build-machine library path.

## 0.8.0

### Features

- A WinHTTP-based `IHttpClient` implementation is now available and is used by default on Windows, removing the mandatory dependency on `libcurl` for Windows builds.
- The SDK will now generate an anonymous user ID, persisting the same value across launches, and attaching that ID to RUM and Log events. Configure the SDK with `SetTrackAnonymousUser(false)` to omit the `anonymous_id` value from all events.
- RUM can now be configured with an optional `RumContextReceiver` callback function, which will be notified of changes to internally-generated RUM values like `session_id`, `view_id`, or `anonymous_id`.

### Fixes

- Errors can now be reported with a custom fingerprint attribute that overrides the default grouping behavior in Error Tracking.
- Error events now include a `source_type` property extracted from the `_dd.error.source_type` attribute, enabling cross-platform SDKs to indicate the format of the supplied stack trace.
- Transport-level HTTP errors (such as TLS certificate failures) are now included in diagnostic log output as `error_code` and `error_message` fields, making it easier to diagnose why requests are failing.

## 0.7.1

### Fixes

- RUM View events now include a `session.is_active` field.

## 0.7.0

### Breaking Changes

- The `source` value reported by the SDK has changed to `cpp`. This requires no changes to application code, but upgrading to this version is mandatory to ensure continued compatibility. The Datadog backend will no longer accept data produced by older releases of the SDK.

## 0.6.0

### Breaking Changes

- In the C API, `dd_core_config_t` now stores string fields (such as `client_token`, `env`, `version`, and `service`) in fixed-size inline buffers instead of `char*` pointers.

### Features

- Time to Initial Display (TTID) can now be reported by calling `Rum::ReportAppDisplayInitialized()` when your app begins rendering its first frame.
- Time to Full Display (TTFD) can now be reported by calling `Rum::ReportAppFullyDisplayed()` after reporting TTID, at a point where the first frame is displayed to the user in a complete form.
- Hitches in main-thread performance can now be reported as RUM Long Tasks, by calling `Rum::AddLongTask()`.
- The SDK now officially supports 32-bit CPU architectures (e.g., x86/i386 and ARM) on Linux.

### Fixes

- Stack frames in the POSIX crash handler now correctly resolve return addresses on ARM64 devices that use Pointer Authentication Codes (PAC), fixing cases where frames could not be mapped to their originating modules.
- The pending-consent storage directory is now subject to the same 18-hour age-based eviction as the granted-consent directory, preventing unbounded disk growth when tracking consent is never granted.
- Event storage directories are now subject to a 512 MB whole-directory size cap; when the limit is exceeded, the oldest batch files are deleted before new data is written.
- Individual events larger than 512 kB are now rejected and dropped rather than being written to disk.

## 0.5.0

### Breaking Changes

- `dd_rum_start_view_obj()` and `dd_rum_stop_view_obj()` functions have been removed. Use `dd_rum_start_view()` and `dd_rum_stop_view()` instead.
- `datadog::CoreConfig::SetApplicationVersion()` is renamed to `datadog::CoreConfig::SetVersion()` (and `dd_core_config_set_application_version()` is now `dd_core_config_set_version()`).
- The `datadog_install()` CMake convenience function now requires keyword arguments `RUNTIME_DESTINATION` and `LIBRARY_DESTINATION`. Replace `datadog_install(bin)` with `datadog_install(RUNTIME_DESTINATION bin)`.

### Features

- Pre-built macOS release artifacts are now universal binaries containing both `arm64` and `x86_64` slices.
- Added support for the `us2-fed` site.

### Fixes

- `datadog_install()` now automatically deploys the SDK shared library alongside the application and configures the correct runtime search path (`RPATH`) on POSIX platforms.
- Strings passed as `name` or `key` to the RUM Operations API are now safely copied, preventing potential crashes or undefined behavior if the original string is destroyed or modified after the call returns.
- Fixed compilation errors when building for 32-bit ARMv7 targets caused by C++ type-deduction issues.

## 0.4.0

### Breaking Changes

- When recording a log message via `dd_logger_<level>()` or `datadog::Logger::<Level>()`, the API allows an `err` parameter to be passed regardless of log level.
- The `err` value passed in log calls now accepts a `message` string, which will be included in resulting log and RUM events as `error.message`.

### Fixes

- When a logger forwards error details to RUM, RUM now correctly populates `error.message` from the error message rather than the log message text.

## 0.3.0

> [!NOTE]
> Initial preview release of the Datadog C++ SDK.
> While at v0, breaking API changes may be introduced in minor-version releases.

### Features

- The SDK may be incorporated into an existing CMake project via `FetchContent` or `find_package`.
- APIs are available for both C and C++.
- The SDK may be configured and initialized via `datadog::Core` or `dd_core_t`.
- Diagnostic output will be written to `stderr` by default; use `SetDiagnosticHandler()` to reroute or suppress this output.
- Transient data will be stored under `.datadog/` in the working directory; use `SetApplicationStoragePath()` to configure an explicit path that's unique to your application.
- Support for user tracking consent is fully implemented: data will not be stored when consent is revoked, and data will only be uploaded once consent is granted.
- Use the **RUM** feature to track views, actions, resources, errors, and operations.
- Use the **Logging** feature to send remote log messages to Datadog.
- Use the **Crash Reporting** feature to detect crashes and report them as RUM Errors on next launch.
