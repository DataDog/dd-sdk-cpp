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
