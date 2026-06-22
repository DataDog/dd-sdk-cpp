## 0.4.1

### Breaking Changes

- The `dd_rum_start_view_obj()` and `dd_rum_stop_view_obj()` functions have been removed; use `dd_rum_start_view()` and `dd_rum_stop_view()` instead, passing `NULL` for the attributes argument when no custom attributes are needed.
- `datadog::CoreConfig::SetApplicationVersion()` is renamed to `datadog::CoreConfig::SetVersion()` (and the C API `dd_core_config_set_application_version()` to `dd_core_config_set_version()`) to align with unified tagging conventions used by other Datadog SDKs.
- The `datadog_install()` CMake function now accepts named `RUNTIME_DESTINATION` and `LIBRARY_DESTINATION` keyword arguments instead of a single positional directory argument, defaulting to `bin` and `lib` respectively.

### Features

- `datadog_install()` now automatically deploys the SDK shared library alongside the application and configures the correct `RPATH` on POSIX systems when using a shared-library build (`DD_BUILD_SHARED=ON`).
- Added support for the `gov2` datacenter (`us2-fed` site) with the `browser-intake-us2-ddog-gov.com` endpoint.
- Building the SDK from source without specifying `CMAKE_BUILD_TYPE` now defaults to `RelWithDebInfo` (or `Debug` when `DD_DEVELOPMENT=ON`) instead of producing an unoptimized, symbolless binary.

### Fixes

- Trace context attributes (`_dd.trace_id`, `_dd.span_id`, `_dd.parent_span_id`, `_dd.rule_psr`) set on RUM resource scopes are now correctly mapped to the corresponding fields on `RumResourceEvent` and `RumErrorEvent` instead of being left in the generic `context` object.
- Fixed a potential crash that could occur when string values passed to the RUM Operations API (`name` or `key`) were destroyed or modified shortly after the call.

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
