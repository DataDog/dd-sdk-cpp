# Remove libdatadog from dd-win-prof; dd-sdk-cpp owns pprof serialization + upload

Goal: dd-win-prof becomes a sampler only (stack capture, symbolication, RUM context). dd-sdk-cpp builds pprof, wraps it in the profiling intake request, and uploads it. Build then needs no libdatadog zip, no `datadog_profiling_ffi.dll`.

Separate from `.plans/win-prof-integration.md`. That plan ships first and keeps libdatadog as a transitive dep until this lands.

## What dd-win-prof uses from libdatadog (inventory, from /tmp clone @ 3359e36)
Only 4 files touch it: `PprofAggregator.{h,cpp}`, `ProfileExporter.{h,cpp}` (1620 lines), `Symbolication.{h,cpp}`, `LibDatadogHelper.h`. ~55 distinct `ddog_*` symbols, two areas:

1. Aggregation / serialization (`ddog_prof_Profile_*`, `ManagedStringStorage_*`)
   - Profile with sample types + period; interning of strings, functions, mappings, locations, stacktraces, labelsets (str + num labels), samples.
   - `ManagedStringStorage`: process-lifetime string table whose ids survive `Profile_reset` (Symbolication caches ids across profiles).
   - `Profile_serialize` -> `EncodedProfile` (compressed pprof protobuf + start/end time); `Profile_reset`.
2. Export (`ddog_prof_Exporter_*`, `Endpoint_*`, `Vec_Tag_*`)
   - Endpoint: agent URL vs agentless (URL + API key); timeout.
   - Stable tags (language, runtime, hardware, service/env/version/host, user tags); per-export tags.
   - `Request_build`: multipart request = pprof part + `event.json` (tags, time range, family, `internal` metadata JSON carrying RUM records, optional info JSON); user-agent, profiler version headers.
   - `Exporter_send`: blocking send, TLS, status handling (dd-win-prof counts consecutive errors).

## SDK side: what we already have / lack
- Have: `IHttpClient::Post` (chunked, libcurl, vendored on Windows), `HttpBodyWriter`, JSON writers (`impl/types/json`), upload thread/scheduler, tracking consent, config.
- Lack: multipart/form-data body writer; compression (no zlib/zstd dep in SDK; check whether vendored libcurl on Windows already brings zlib); pprof protobuf encoder; any large-payload path (current storage queue is sized for RUM/log batches).

## Phases
0. **Spec + golden fixtures** (no code ships). Read libdatadog v19 `profiling` crate source for exact intake contract: multipart part names/filenames, `event.json` schema, headers (API key, EVP origin headers, user-agent), compression (zstd vs gzip — confirm intake accepts gzip/none; dd-trace-go uses gzip), size limits. Capture fixtures: run dd-win-prof Runner against a local echo server (repo has `echo_server`) to record real requests + decoded pprof.
1. **Seam inside dd-win-prof** (upstream, behavior-preserving). Introduce libdatadog-free data model: `Sample` with resolved frames (function, file, line, module, build id) + labels + values. Define `IProfileSink` (`OnProfile(...)`) between sampling and export. Existing libdatadog path becomes one implementation of it. Existing Tests keep passing.
2. **pprof encoder in dd-sdk-cpp** (`src/datadog/impl/profiling/pprof/`). Hand-rolled protobuf wire writer (pprof proto is ~8 small messages; no protobuf lib). String table + function/mapping/location/stack dedupe, string table that persists across profiles (replaces ManagedStringStorage). Compression per phase 0 (gzip via zlib, or stored-deflate fallback if intake allows uncompressed). Tests: decode with python `protobuf`/`pprof` tool; diff against libdatadog output for the same samples (semantic compare, not byte compare).
3. **Exporter in dd-sdk-cpp**. Multipart `HttpBodyWriter`, `event.json` via SDK JSON writer, headers/tags, endpoint from SDK config (site, client token / API key mode), consent gating. Decide send path: direct `IHttpClient::Post` on a dedicated thread vs disk-backed upload queue (retry, backoff, size caps). Tests with fake `IHttpClient`.
4. **New dd-win-prof boundary.** Versioned (size-prefixed) C API to register a sample/profile callback; dd-win-prof delivers batches (resolved frames, labels, values, time range, RUM records) to the SDK. SDK Feature (PR #228) consumes them. Keep `SetupProfiler/StartProfiler/StopProfiler/EnterView/LeaveCurrentView` shape.
5. **Drop libdatadog from build.** dd-win-prof option `DD_WIN_PROF_USE_LIBDATADOG` (ON top-level = standalone product keeps working; OFF as dependency = sampler-only). SDK CMake sets it OFF. Remove `libdatadog_*` targets, ffi DLL copy/install, x64-only-zip constraint from our integration; update `LICENSE-3rdparty.csv`, plan 1 steps (DLL list shrinks to dd-win-prof.dll).
6. **Cutover + soak**: A/B compare backend profiles from both paths on same workload; remove libdatadog path from SDK integration only (standalone dd-win-prof unaffected).

## Risks
- pprof subtleties: string_table[0]="", 1-based ids, location/mapping/function dedupe, period/duration/time_nanos, sample_type order must match sample value order, label numeric units. Wrong output often "uploads fine, renders wrong".
- Compression format intake accepts (unverified).
- libdatadog behaviors easy to miss: retry/timeouts, proxy env vars, TLS roots (Rust rustls vs libcurl/Schannel), request size limits, tag sanitization/normalization.
- Memory: persistent string table + large profiles vs SDK's small-batch queue design.
- Two upload stacks during transition (standalone dd-win-prof vs SDK).
- Cross-repo coordination (dd-win-prof owners must accept phases 1, 4, 5).

## Unresolved questions
- Can dd-win-prof keep a libdatadog build for standalone use (phase 5 option), or should libdatadog be deleted upstream entirely?
- Who owns the intake contract (profiling backend team) — can they confirm accepted compression, size limits, and whether `event.json` fields can be produced independently of libdatadog?
- Does the SDK want profiles to go through the on-disk upload queue (retry/consent) or a direct-send path?
- Symbolication stays in dd-win-prof (SDK receives strings) or moves into SDK (SDK receives addresses + module list)? Plan assumes stays.
- Is hand-rolled protobuf acceptable, or is a vendored lib (e.g. nanopb/upb) preferred?
- Compression dependency: add zlib/zstd to SDK deps, or reuse what vendored libcurl brings?
- Target scope: x64 only (stack walking is likely x64-specific anyway)?
- Timeline/priority relative to the integration plan and PR #228?
