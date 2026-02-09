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
#include <windows.h>
#include <tlhelp32.h>  // ToolHelp32 snapshot APIs for enumerating loaded modules
// clang-format on

#include <cinttypes>
#include <ctime>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/crash_handler.hpp"

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
static char s_crash_filename[MAX_PATH];  // Path to crash report file

// Original exception filter to restore on shutdown
static LPTOP_LEVEL_EXCEPTION_FILTER s_old_filter = nullptr;

// === Exception-safe helpers for string formatting and file I/O ===

/**
 * Writes a null-terminated string to the given file handle. Safe to call from exception
 * handler context.
 */
static void write_str(HANDLE file, const char* str) {
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }

  // Compute string length manually since we can't rely on strlen in exception context
  DWORD len = 0;
  while (str[len]) {
    len++;
  }

  // Write to the open file handle, ignoring result since exception handler has no
  // recovery path for I/O errors
  DWORD written = 0;
  WriteFile(file, str, len, &written, nullptr);
}

/**
 * Writes a literal newline character to the given file handle.
 */
static void write_newline(HANDLE file) {
  DWORD written = 0;
  WriteFile(file, "\n", 1, &written, nullptr);
}

/**
 * Writes an unsigned, 64-bit integer to the given file handle as an ASCII string, in
 * decimal format. Safe to call from exception handler context.
 */
static void write_uint64(HANDLE file, uint64_t val) {
  // Early-out for 0: write literal '0', ignoring result
  if (val == 0) {
    write_str(file, "0");
    return;
  }

  // Establish a buffer large enough to contain the max possible uint64_t
  char buf[32];
  int i = 0;

  // sprintf/snprintf are not safe in exception context: manually accumulate the digits
  // of our decimal value into the buffer, from least-significant to most-significant
  while (val > 0) {
    buf[i++] = static_cast<char>('0' + (val % 10));
    val /= 10;
  }

  // Iterate backwards to write each of our digits into the file in reverse, ignoring
  // the result since the exception handler has no recovery path for I/O errors
  while (i > 0) {
    DWORD written = 0;
    WriteFile(file, &buf[--i], 1, &written, nullptr);
  }
}

/**
 * Writes an unsigned, 32-bit integer to the given file handle as an ASCII string, in
 * decimal format. Safe to call from exception handler context.
 */
static void write_uint(HANDLE file, uint32_t val) { write_uint64(file, val); }

/**
 * Writes an address to the given file handle in lowercase hex format, prefixed with
 * '0x', always padded with leading zeroes to 16 hex digits (64-bit addresses). Safe to
 * call from exception handler context.
 */
static void write_hex_address(HANDLE file, uintptr_t addr) {
  // Establish a constant lookup table of hex digits, and a buffer large enough to
  // contain "0x" + 16 hex digits
  const char hex_chars[] = "0123456789abcdef";
  char buf[18];

  // Write the 2-byte prefix to the buffer
  int i = 0;
  buf[i++] = '0';
  buf[i++] = 'x';

  // sprintf/snprintf are not safe in exception context: manually convert to hex by
  // starting at the top nibble (bits 60-63), masking those 4 bits to resolve the most
  // significant hex digit, then progressing forward toward less-significant digits,
  // stepping down by 4 bits each time
  for (int shift = 60; shift >= 0; shift -= 4) {
    // Write the appropriate hex digit into the next position in our buffer
    buf[i++] = hex_chars[(addr >> shift) & 0xf];
  }

  // Write the 18 bytes from our buffer to the open file handle, ignoring the result
  // since the exception handler has no recovery path for I/O errors
  DWORD written = 0;
  WriteFile(file, buf, i, &written, nullptr);
}

/**
 * Writes a 32-bit value to the given file handle in uppercase hex format, prefixed with
 * '0x', always padded with leading zeroes to 8 hex digits. Does NOT write a trailing
 * newline. Safe to call from exception handler context.
 */
static void write_hex_dword(HANDLE file, DWORD val) {
  // Establish a constant lookup table of hex digits (uppercase for consistency with
  // Windows convention), and a buffer large enough to contain "0x" + 8 hex digits
  const char hex_chars[] = "0123456789ABCDEF";
  char buf[10];

  // Write the 2-byte prefix to the buffer
  int i = 0;
  buf[i++] = '0';
  buf[i++] = 'x';

  // Manually convert to hex by starting at the top nibble (bits 28-31), masking those 4
  // bits to resolve the most significant hex digit, then progressing forward
  for (int shift = 28; shift >= 0; shift -= 4) {
    buf[i++] = hex_chars[(val >> shift) & 0xf];
  }

  // Write the 10 bytes from our buffer to the open file handle
  DWORD written = 0;
  WriteFile(file, buf, i, &written, nullptr);
}

// === Enumeration of loaded modules ===
// - Uses ToolHelp32 snapshot API to enumerate PE modules loaded in the current process

/**
 * Resolves a list of all modules that are loaded in the current process, and writes
 * them to the given file handle, in the format:
 *
 * <start-address>-<end-address> <binary-path>
 *
 * Where addresses are encoded as '0x'-prefixed, lowercase hex values zero-padded to 16
 * bytes, and binary path is the full path to the executable or DLL file.
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
    write_str(file, "[ERROR: Failed to create module snapshot]");
    write_newline(file);
    return;
  }

  // Module32First/Module32Next iterate through modules in the snapshot, populating a
  // MODULEENTRY32 struct for each: we must set dwSize before calling Module32First
  MODULEENTRY32 me;
  me.dwSize = sizeof(me);

  if (!Module32First(snapshot, &me)) {
    write_str(file, "[ERROR: Failed to enumerate modules]");
    write_newline(file);
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

    // Write the module information in format: 0x<base>-0x<end> <path>
    write_hex_address(file, base_addr);
    write_str(file, "-");
    write_hex_address(file, end_addr);
    write_str(file, " ");
    write_str(file, me.szExePath);
    write_newline(file);
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
  // We should have a file handle for our crash report file, opened when crash reporting
  // was initialized. If we have no such file, return EXCEPTION_EXECUTE_HANDLER to let
  // Windows terminate the process.
  HANDLE file = s_crash_file;
  if (file == INVALID_HANDLE_VALUE) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  write_str(file, "=== Datadog SDK Crash Report ===\n");

  // The EXCEPTION_POINTERS structure contains two key components:
  // - ExceptionRecord: details about the exception (code, address, flags)
  // - ContextRecord: CPU register state at the time of the exception
  write_str(file, "Exception: ");

  // Exception codes are 32-bit values defined by Windows, with different ranges for
  // different exception categories. The high 2 bits indicate severity:
  // - 00 = Success, 01 = Informational, 10 = Warning, 11 = Error
  const DWORD code = exinfo->ExceptionRecord->ExceptionCode;
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
      write_str(file, "ACCESS_VIOLATION");
      break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      write_str(file, "ARRAY_BOUNDS_EXCEEDED");
      break;
    case EXCEPTION_BREAKPOINT:
      write_str(file, "BREAKPOINT");
      break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
      write_str(file, "DATATYPE_MISALIGNMENT");
      break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
      write_str(file, "FLT_DENORMAL_OPERAND");
      break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      write_str(file, "FLT_DIVIDE_BY_ZERO");
      break;
    case EXCEPTION_FLT_INEXACT_RESULT:
      write_str(file, "FLT_INEXACT_RESULT");
      break;
    case EXCEPTION_FLT_INVALID_OPERATION:
      write_str(file, "FLT_INVALID_OPERATION");
      break;
    case EXCEPTION_FLT_OVERFLOW:
      write_str(file, "FLT_OVERFLOW");
      break;
    case EXCEPTION_FLT_STACK_CHECK:
      write_str(file, "FLT_STACK_CHECK");
      break;
    case EXCEPTION_FLT_UNDERFLOW:
      write_str(file, "FLT_UNDERFLOW");
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      write_str(file, "ILLEGAL_INSTRUCTION");
      break;
    case EXCEPTION_IN_PAGE_ERROR:
      write_str(file, "IN_PAGE_ERROR");
      break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      write_str(file, "INT_DIVIDE_BY_ZERO");
      break;
    case EXCEPTION_INT_OVERFLOW:
      write_str(file, "INT_OVERFLOW");
      break;
    case EXCEPTION_INVALID_DISPOSITION:
      write_str(file, "INVALID_DISPOSITION");
      break;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
      write_str(file, "NONCONTINUABLE_EXCEPTION");
      break;
    case EXCEPTION_PRIV_INSTRUCTION:
      write_str(file, "PRIV_INSTRUCTION");
      break;
    case EXCEPTION_SINGLE_STEP:
      write_str(file, "SINGLE_STEP");
      break;
    case EXCEPTION_STACK_OVERFLOW:
      write_str(file, "STACK_OVERFLOW");
      break;
    default:
      write_str(file, "UNKNOWN (");
      write_hex_dword(file, code);
      write_str(file, ")");
      break;
  }
  write_newline(file);

  write_str(file, "Exception code: ");
  write_hex_dword(file, code);
  write_newline(file);

  // ExceptionAddress points to the instruction that caused the exception: this is the
  // actual crash location and should be the first address in our stack trace
  write_str(file, "Exception address: ");
  write_hex_address(
      file, reinterpret_cast<uintptr_t>(exinfo->ExceptionRecord->ExceptionAddress)
  );
  write_newline(file);

  // ExceptionFlags indicates whether the exception is continuable:
  // - 0 = continuable exception (handler could theoretically fix and continue)
  // - EXCEPTION_NONCONTINUABLE (0x1) = noncontinuable (must terminate)
  write_str(file, "Exception flags: ");
  write_hex_dword(file, exinfo->ExceptionRecord->ExceptionFlags);
  write_newline(file);

  write_str(file, "PID: ");
  write_uint(file, GetCurrentProcessId());
  write_newline(file);

  write_str(file, "TID: ");
  write_uint(file, GetCurrentThreadId());
  write_newline(file);

  // Timestamp (Unix epoch format for consistency with POSIX implementation)
  time_t now = time(nullptr);
  write_str(file, "Timestamp: ");
  write_uint64(file, now);
  write_newline(file);

  // === Stack trace capture ===
  // Windows provides RtlCaptureStackBackTrace for simple stack walking without needing
  // to manually parse frame pointers. This function walks the stack using frame
  // pointers (if available) or unwind metadata from the PE's .pdata section.
  write_str(file, "\nStack trace (raw addresses):\n");

  // Capture the call stack using RtlCaptureStackBackTrace, which returns an array of
  // return addresses for each frame. We skip the first frame (frame 0), which would be
  // the call to RtlCaptureStackBackTrace itself. Note that the result still includes
  // additional SEH-related stack frames beyond the point where the crash occurred.
  const int max_frames = 128;
  void* stack[max_frames];
  const USHORT frames = RtlCaptureStackBackTrace(1, max_frames, stack, nullptr);
  for (USHORT i = 0; i < frames; i++) {
    write_hex_address(file, reinterpret_cast<uintptr_t>(stack[i]));
    write_newline(file);
  }

  // Write loaded modules
  write_str(file, "\nLoaded Modules:\n");
  write_modules(file);

  write_str(file, "\n=== End of crash report ===\n");

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
    // and the timestamp is SDK start time (to ensure uniqueness), not crash time
    time_t now = time(nullptr);
    struct tm tm_buf;
    if (localtime_s(&tm_buf, &now) != 0) {
      _logger.Error("Failed to get local time");
      return false;
    }
    _snprintf_s(
        s_crash_filename,
        sizeof(s_crash_filename),
        _TRUNCATE,
        ".crashes\\crash_%04d%02d%02d_%02d%02d%02d_%lu.txt",
        tm_buf.tm_year + 1900,
        tm_buf.tm_mon + 1,
        tm_buf.tm_mday,
        tm_buf.tm_hour,
        tm_buf.tm_min,
        tm_buf.tm_sec,
        static_cast<unsigned long>(GetCurrentProcessId())
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
