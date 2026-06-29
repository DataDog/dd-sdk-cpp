## 0.5.0

### Breaking Changes

- The `dd_rum_start_view_obj()` and `dd_rum_stop_view_obj()` functions have been removed; use `dd_rum_start_view()` and `dd_rum_stop_view()` instead, passing `NULL` for the `attributes` parameter when no custom attributes are needed.
- `datadog::CoreConfig::SetApplicationVersion()` is renamed to `datadog::CoreConfig::SetVersion()` (and the C API `dd_core_config_set_application_version()` to `dd_core_config_set_version()`) to align with the `version` unified service tagging convention used by other Datadog SDKs.
- The `datadog_install()` CMake convenience function now accepts named keyword arguments `RUNTIME_DESTINATION` and `LIBRARY_DESTINATION` instead of a single positional output directory argument (e.g. replace `datadog_install(bin)` with `datadog_install(RUNTIME_DESTINATION bin)`).

### Features

- `datadog_install()` now automatically deploys the SDK shared library alongside the application and configures the correct runtime search path (`RPATH`) on POSIX platforms, so that installed applications can load the SDK at runtime without additional manual CMake configuration.
- Pre-built macOS release artifacts are now universal binaries containing both `arm64` and `x86_64` slices, with artifact names updated to use a `macos-universal` identifier.
- Added support for the `gov2` datacenter (site `us2-fed`, TLD `browser-intake-us2-ddog-gov.com`).
- UUID generation is now pluggable: platforms without a built-in UUID generator can supply their own implementation, while existing platforms continue to use their built-in generator by default.
- When building the SDK from source without specifying `CMAKE_BUILD_TYPE`, a sensible default is now assumed (`RelWithDebInfo` for normal builds, `Debug` when `DD_DEVELOPMENT=ON`), preventing accidental unoptimized, symbol-less builds.

### Fixes

- RUM Resource events and Resource error events now correctly propagate trace context attributes (`_dd.trace_id`, `_dd.span_id`, `_dd.parent_span_id`, `_dd.rule_psr`) to their dedicated event fields rather than leaving them in the generic `context` object.
- Strings passed as `name` or `key` to the RUM Operations API are now safely copied, preventing potential crashes or undefined behavior if the original string is destroyed or modified after the call returns.
- Fixed compilation errors when building for 32-bit ARMv7 targets caused by C++ type-deduction issues with diagnostic attribute values, timestamp storage, and upload backoff calculations.

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
