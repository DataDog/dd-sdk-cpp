// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// Windows exception handling uses Structured Exception Handling (SEH), which provides
// EXCEPTION_POINTERS containing register state (CONTEXT) and exception details
// (EXCEPTION_RECORD) when unhandled exceptions occur. We use
// SetUnhandledExceptionFilter to register crash_exception_filter() as the top-level
// handler.

// clang-format off
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>  // ToolHelp32 snapshot APIs for enumerating loaded modules
// clang-format on

#include <cinttypes>
#include <ctime>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/crash_context_write.hpp"
#include "datadog/impl/platform/crash_handler.hpp"
#include "datadog/impl/platform/crash_handler_buildid_cache.hpp"
#include "datadog/impl/platform/crash_report_write.hpp"

namespace datadog::platform {

// We only officially support 64-bit architectures; this code may need to be revisited
// if we add legacy 32-bit support
static_assert(
    sizeof(uint64_t) == sizeof(uintptr_t),
    "Unexpected uintptr_t size; Windows crash handler assumes 64-bit"
);

// === Global state ===
// - Preemptively initialized in InProcessCrashHandler::Initialize(), when it's still
//   safe to allocate, call ordinary system functions, etc.
// - Subsequently used during crash, where we're constrained to exception-safe routines

// Pre-opened file handle for crash report file to be written
static HANDLE s_crash_file = INVALID_HANDLE_VALUE;
static char s_crash_filename[MAX_PATH];          // Path to crash report file
static char s_crash_context_filename[MAX_PATH];  // Path to companion crash context file

// Original exception filter to restore on shutdown
static LPTOP_LEVEL_EXCEPTION_FILTER s_old_filter = nullptr;

// Atomically set to 1 once our exception filter has been called, and never reset
static LONG s_in_filter = 0;

// === Enumeration of loaded modules ===
// - Uses ToolHelp32 snapshot API to enumerate PE modules loaded in the current process

/**
 * Resolves a list of all modules that are loaded in the current process, and writes
 * them to the given file handle in binary format using WriteCrashReportModule().
 */
static void write_modules(HANDLE file) {
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }

  // Use the ToolHelp32 API to create a snapshot of all modules loaded in the current
  // process: TH32CS_SNAPMODULE captures 64-bit modules, while TH32CS_SNAPMODULE32
  // captures any 32-bit modules running under WOW64 (generally not applicable for our
  // 64-bit-only SDK, but included for completeness)
  HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId()
  );

  if (snapshot == INVALID_HANDLE_VALUE) {
    // Failed to create module snapshot; silently return (best-effort crash handling)
    return;
  }

  // Module32First/Module32Next iterate through modules in the snapshot, populating a
  // MODULEENTRY32 struct for each: we must set dwSize before calling Module32First
  MODULEENTRY32 me;
  me.dwSize = sizeof(me);

  if (!Module32First(snapshot, &me)) {
    // Failed to enumerate modules; silently return (best-effort crash handling)
    CloseHandle(snapshot);
    return;
  }

  // Iterate through all loaded modules, writing their address ranges and paths
  do {
    // Each MODULEENTRY32 contains:
    // - modBaseAddr: base address where the module is loaded in virtual memory
    // - modBaseSize: size of the module in bytes
    // - szExePath: full path to the PE executable/DLL file
    const uintptr_t base_addr = reinterpret_cast<uintptr_t>(me.modBaseAddr);
    const uintptr_t end_addr = base_addr + me.modBaseSize;

    // Retrieve build ID from cache
    const char* build_id = FindCachedBuildId(base_addr);

    // Write relevant details for this module
    WriteCrashReportModule(
        file,
        static_cast<uint64_t>(base_addr),
        static_cast<uint64_t>(end_addr),
        me.szExePath,
        build_id
    );
  } while (Module32Next(snapshot, &me));

  // Clean up the snapshot handle
  CloseHandle(snapshot);
}

/**
 * Main exception filter used to dump crash reports to disk for later processing by the
 * SDK. Invoked when the process encounters an unhandled exception and has no other
 * exception handlers registered.
 *
 * Must be entirely safe to call from exception context: no C++ exceptions, no
 * allocations that could fail, minimal dependencies on C runtime.
 */
static LONG WINAPI crash_exception_filter(EXCEPTION_POINTERS* exinfo) {
  // One-shot reentrancy guard: if this function is called more than once (e.g. because
  // a second exception is raised during handling of the first), we'll take no action
  // and let Windows continue handling the new exception as normal
  if (InterlockedExchange(&s_in_filter, 1) != 0) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // We should have a file handle for our crash report file, opened when crash reporting
  // was initialized. If we have no such file, return EXCEPTION_EXECUTE_HANDLER to let
  // Windows terminate the process.
  HANDLE file = s_crash_file;
  if (file == INVALID_HANDLE_VALUE) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  // The EXCEPTION_POINTERS structure contains two key components:
  // - ExceptionRecord: details about the exception (code, address, flags)
  // - ContextRecord: CPU register state at the time of the exception

  // Exception codes are 32-bit values defined by Windows, with different ranges for
  // different exception categories. The high 2 bits indicate severity:
  // - 00 = Success, 01 = Informational, 10 = Warning, 11 = Error
  const DWORD code = exinfo->ExceptionRecord->ExceptionCode;

  // Write file header with exception details
  WriteCrashReportHeader(
      file,
      static_cast<uint64_t>(code),  // fault_code (exception code)
      reinterpret_cast<uint64_t>(
          exinfo->ExceptionRecord->ExceptionAddress
      ),                                                               // fault_address
      static_cast<uint64_t>(exinfo->ExceptionRecord->ExceptionFlags),  // fault_flags
      static_cast<uint64_t>(GetCurrentProcessId()),                    // pid
      static_cast<uint64_t>(GetCurrentThreadId()),                     // tid
      static_cast<uint64_t>(time(nullptr))                             // timestamp
  );

  // Write loaded modules
  write_modules(file);

  // === Stack trace capture ===
  // Windows provides RtlCaptureStackBackTrace for simple stack walking without needing
  // to manually parse frame pointers. This function walks the stack using frame
  // pointers (if available) or unwind metadata from the PE's .pdata section.

  // Capture the call stack using RtlCaptureStackBackTrace, which returns an array of
  // return addresses for each frame. We skip the first frame (frame 0), which would be
  // the call to RtlCaptureStackBackTrace itself. Note that the result still includes
  // additional SEH-related stack frames beyond the point where the crash occurred.
  const int max_frames = 128;
  void* stack[max_frames];
  const USHORT frames = RtlCaptureStackBackTrace(1, max_frames, stack, nullptr);
  for (USHORT i = 0; i < frames; i++) {
    WriteCrashReportStackFrame(file, reinterpret_cast<uint64_t>(stack[i]));
  }

  // Write footer to mark end of crash report
  WriteCrashReportFooter(file);

  // Intentionally leave the file handler open: CloseHandle could block or fail in an
  // exception context. Windows will clean up on process termination.

  // If we replaced a previously-registered unhandled-exception filter, chain the call
  // to that function
  if (s_old_filter != nullptr) {
    // If the previously-registered function indicates that it's handled the exception,
    // respect that result
    const LONG old_filter_result = s_old_filter(exinfo);
    if (old_filter_result != EXCEPTION_CONTINUE_SEARCH) {
      return old_filter_result;
    }
  }

  // If the exception wasn't decisively handled by any previous exception filter,
  // terminate the process: if we've handled what we consider a fatal, reportable crash,
  // then we should ensure that the application is shut down. Convey the exception code
  // as the process exit code (which is a uint32 DWORD on Windows)
  TerminateProcess(GetCurrentProcess(), code);

  // Unreachable fallback: we own the crash outcome, hence EXCEPTION_EXECUTE_HANDLER
  return EXCEPTION_EXECUTE_HANDLER;
}

/**
 * Windows implementation of an in-process crash handler using Structured Exception
 * Handling (SEH).
 *
 * With this implementation:
 *
 * - Initialize() preemptively opens a crash report file, then registers
 *   crash_exception_filter() as the top-level unhandled exception filter via
 *   SetUnhandledExceptionFilter(), such that it will be called for any unhandled
 *   exceptions.
 *
 *   - crash_exception_filter() examines exception details, reconstructs the callstack
 *     using RtlCaptureStackBackTrace, resolves the paths and address ranges of loaded
 *     PE modules, and writes that information to the crash report file before allowing
 *     Windows to terminate the process.
 *
 * - Shutdown() handles clean SDK shutdown by unregistering the exception filter, then
 *   closing and deleting the unused crash report file.
 */
class InProcessCrashHandler final : public ICrashHandler {
 public:
  explicit InProcessCrashHandler(impl::DiagnosticLogger& logger) : _logger(logger) {}

  /**
   * Initializes in-process crash handling by pre-opening a timestamped, PID-tagged
   * crash report file and registering an unhandled exception filter. If all steps
   * succeed, returns true; if any step fails, restores original state and returns
   * false.
   */
  bool Initialize() override {
    // Set up the crash handler in stages, cleaning up on failure at each step
    DATADOG_ASSERT(!_initialized, "InProcessCrashHandler::Initialize called twice");

    // Create directory to contain crashes, aborting on failure
    // TODO(WIP): Store crashes relative to SDK storage root
    // Using CreateDirectoryA (narrow/ANSI API) for simplicity: this may fail with
    // non-ASCII paths, which is a known limitation
    if (!CreateDirectoryA(".crashes", nullptr)) {
      const DWORD err = GetLastError();
      if (err != ERROR_ALREADY_EXISTS) {
        _logger.Error("Failed to create .crashes directory");
        return false;
      }
    }

    // Format a filename for this process's crash report file, with timestamp and PID:
    // note that the mere presence of this file does not indicate that a crash occurred,
    // and the timestamp is SDK start time (to ensure uniqueness), not crash time.
    // Format: crash_<system-timestamp-in-ms>_<pid>
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    // Convert from 100-nanosecond intervals since 1601 to milliseconds since Unix epoch
    uint64_t timestamp_ms = (uli.QuadPart / 10000) - 11644473600000ULL;
    _snprintf_s(
        s_crash_filename,
        sizeof(s_crash_filename),
        _TRUNCATE,
        ".crashes\\crash_%" PRIu64 "_%lu",
        timestamp_ms,
        static_cast<unsigned long>(GetCurrentProcessId())
    );
    _snprintf_s(
        s_crash_context_filename,
        sizeof(s_crash_context_filename),
        _TRUNCATE,
        "%s.ctx",
        s_crash_filename
    );

    // Preemptively open the crash report file and keep it open indefinitely
    s_crash_file = CreateFileA(
        s_crash_filename,
        GENERIC_WRITE,
        FILE_SHARE_READ,  // Allow other processes to read the file while we write
        nullptr,          // No security attributes
        CREATE_ALWAYS,    // Always create new file, overwriting if it exists
        FILE_ATTRIBUTE_NORMAL,
        nullptr  // No template file
    );

    if (s_crash_file == INVALID_HANDLE_VALUE) {
      _logger.Error("Failed to create crash file");
      return false;
    }

    // Install our unhandled exception filter: SetUnhandledExceptionFilter sets a
    // top-level exception handler that's called when no other handlers catch an
    // exception. Returns the previous filter, which we save for restoration on
    // shutdown.
    //
    // LIMITATION: Other code in the process (including third-party libraries) can call
    // SetUnhandledExceptionFilter and override our handler. For more robust crash
    // handling, consider using Vectored Exception Handlers (VEH) via
    // AddVectoredExceptionHandler, which are called before SEH and cannot be easily
    // overridden.
    s_old_filter = SetUnhandledExceptionFilter(crash_exception_filter);

    _logger.Debug("In-process crash handler initialized successfully");
    _initialized = true;

    // Initialize build ID cache for crash-time lookup
    PopulateBuildIdCache();

    return true;
  }

  /**
   * Uninitializes the handler in the event of a clean shutdown in a process where no
   * crashes occurred.
   */
  void Shutdown() override {
    // If we weren't successfully initialized, there should be no cleanup needed
    if (!_initialized) {
      return;
    }

    // Restore old exception filter that was replaced when we installed ours, if any
    SetUnhandledExceptionFilter(s_old_filter);
    s_old_filter = nullptr;

    // On successful init we should have opened a crash report file: close that file and
    // delete it
    DATADOG_ASSERT(
        s_crash_file != INVALID_HANDLE_VALUE,
        "s_crash_file should be open if crash handler was fully initialized"
    );

    // Check the file size to determine if we wrote any crash data: use GetFileSizeEx to
    // query the file size without seeking
    LARGE_INTEGER file_size;
    const BOOL got_size = GetFileSizeEx(s_crash_file, &file_size);

    // Close the file handle
    CloseHandle(s_crash_file);
    s_crash_file = INVALID_HANDLE_VALUE;

    // If we successfully queried the file size and confirmed it's empty, delete the
    // file. If GetFileSizeEx failed or the file has content, leave it on disk (empty
    // files will be handled cleanly by subsequent SDK instances; non-empty files may
    // contain partial crash data worth investigating).
    if (got_size && file_size.QuadPart == 0) {
      const BOOL deleted = DeleteFileA(s_crash_filename);
      (void)deleted;  // Ignore result; file cleanup is best-effort
    }

    // Delete the crash context file: it's only meaningful if a crash occurred,
    // and on clean shutdown we don't want it to surface as stale context
    DeleteCrashContext(s_crash_context_filename);
  }

  void SetRumContext(const impl::RumFeatureContext& rum_ctx) override {
    WriteCrashContext(s_crash_context_filename, rum_ctx);
  }

 private:
  impl::DiagnosticLogger& _logger;
  bool _initialized{false};
};

std::unique_ptr<ICrashHandler> CrashHandler::Init(
    impl::DiagnosticLogger& logger, std::string_view handler_exe_path
) {
  // The in-process crash handler does not spawn an external executable; we can ignore
  // any configured handler path
  (void)handler_exe_path;

  // Construct a new InProcessCrashHandler: the SDK will call Initialize() if and when
  // it decides to enable crash reporting
  return std::make_unique<InProcessCrashHandler>(logger);
}

}  // namespace datadog::platform
