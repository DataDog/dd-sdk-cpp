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

<!-- ### Internal Changes

- UUID generation is now pluggable: platforms without a built-in UUID generator can supply their own implementation, while existing platforms continue to use their built-in generator by default.
- RUM Resource events and Resource error events now correctly propagate trace context attributes (`_dd.trace_id`, `_dd.span_id`, `_dd.parent_span_id`, `_dd.rule_psr`) to their dedicated event fields rather than leaving them in the generic `context` object.

-->

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
