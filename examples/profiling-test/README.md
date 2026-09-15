# Profiling and RUM smoke test

This Windows-only example starts the native profiler and RUM on one `Core`, then runs
CPU work across three RUM views. Profiles should contain `rum.application_id`,
`rum.session_id`, and per-sample `rum.view_id` labels.

The integration uses dd-win-prof's atomic `SetRumCorrelationContext` API from commit
`1fb2067f27f7e510406f3786fcddaee35689d548`. A local checkout can override that pin.

## Configure and build

Run from a Visual Studio 2022 developer PowerShell at the `win-sync` root:

```powershell
$profilerSource = Resolve-Path .\dd-win-prof-correlation
cmake -S $profilerSource -B "$profilerSource\build" `
  -G "Visual Studio 17 2022" -A x64
cmake --build "$profilerSource\build" --config Release

cd .\dd-sdk-cpp
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DDD_ENABLE_PROFILING=ON `
  -DDD_WIN_PROF_SOURCE_DIR="$profilerSource" `
  -DDD_BUILD_EXAMPLES=ON `
  -DDD_BUILD_TESTING=ON `
  -DDD_HTTP_USE_SYSTEM_LIBCURL=OFF `
  -DDD_DEVELOPMENT=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Run

Create `dd-sdk-cpp\examples\profiling-test\.env`:

```dotenv
DD_CLIENT_TOKEN=<staging RUM client token>
DD_RUM_APPLICATION_ID=<staging RUM application UUID>
DD_API_KEY=<staging API key>
DD_SITE=datad0g.com
DD_INTERNAL_PROFILING_OUTPUT_DIR=C:\temp\dd-profiles
DD_TRACE_LOG_DIRECTORY=C:\temp\dd-profiler-logs
```

Then run:

```powershell
cd .\examples\profiling-test
. .\load-env.ps1
cd ..\..
.\build\examples\profiling-test\Release\profiling_test.exe
```

Verify that `.pprof` files are written under `DD_INTERNAL_PROFILING_OUTPUT_DIR` and that
the profiler logs show RUM session and view transitions. Upload verification requires
staging access and matching RUM/profile credentials.

## Current configuration boundary

RUM uses `DD_CLIENT_TOKEN` and the Core staging intake URL. Profiling uses `DD_API_KEY`
and `DD_SITE` through dd-win-prof. Service, environment, and version are duplicated in
`CoreConfig` and `ProfilerConfig`. This is the current compatibility boundary, not the
intended final API.
