# profiling-test

Minimal smoke test that exercises the dd-sdk-cpp **Profiling + RUM** integration end-to-end.

## What it does

1. Creates a `datadog::Core` (service=`profiling-test`, env=`dev`, staging endpoint)
2. Registers **Profiling** (agentless, symbolize=true) and **RUM**
3. Calls `core->Start()` which auto-wires RUM context into the profiler
4. Simulates 3 view transitions with ~5s of CPU busy-loop each:
   - `HomePage` -> `SettingsPage` -> `ProfilePage`
5. Calls `core->Stop()` and prints a summary

Total runtime: ~15s. The profiler should collect CPU/wall-time samples tagged with the active RUM view.

## Environment variables

| Variable | Read by | Description |
|---|---|---|
| `DD_CLIENT_TOKEN` | Code | Client token (used by Core for RUM) |
| `DD_RUM_APPLICATION_ID` | Code | RUM application ID |
| `DD_API_KEY` | Profiler (env) | Datadog API key for profile upload |
| `DD_SITE` | Profiler (env) | Datadog site, e.g. `datad0g.com` (staging) or `datadoghq.com` (prod) |

Create a `.env` file in this directory (git-ignored):

```
DD_CLIENT_TOKEN=your-client-token-here
DD_RUM_APPLICATION_ID=your-rum-app-id-here
DD_API_KEY=your-api-key-here
DD_SITE=datad0g.com
```

Then source it before running:

```powershell
# Load env vars from .env
. .\load-env.ps1

# Or specify a custom path
. .\load-env.ps1 -Path C:\path\to\.env
```

## Build

From the `dd-sdk-cpp` root:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DDD_ENABLE_PROFILER=ON `
  -DDD_BUILD_EXAMPLES=ON `
  -DDD_HTTP_USE_SYSTEM_LIBCURL=OFF `
  -DDD_WIN_PROF_SOURCE_DIR="$PWD\..\dd-win-prof"

cmake --build build --config Release --target dd_profiling_test
```

## Run

```powershell
.\build\examples\profiling-test\Release\dd_profiling_test.exe
```
