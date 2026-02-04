// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// clang-format off
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <ctime>

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/crash_handler.hpp"

namespace datadog::platform {

// LIMITATION: Using CreateFileA (narrow API) for simplicity.
// May fail with non-ASCII working directory paths.

// LIMITATION: SetUnhandledExceptionFilter() can be overridden by other code.
// Production version should consider Vectored Exception Handlers (VEH).

// BEHAVIOR: Only the first crash per process is captured.
// Subsequent crashes (if handler doesn't exit) use OS default handler.

// Global state for exception handler
static LPTOP_LEVEL_EXCEPTION_FILTER s_old_filter = nullptr;
static char s_crash_filename[MAX_PATH];
static HANDLE s_crash_file = INVALID_HANDLE_VALUE;

// Helper: write a string literal
static void write_str(HANDLE file, const char* str) {
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }

  DWORD len = 0;
  while (str[len]) {
    len++;
  }

  DWORD written = 0;
  WriteFile(file, str, len, &written, nullptr);
}

// Helper: write an unsigned integer as decimal
static void write_uint(HANDLE file, unsigned long val) {
  char buf[32];
  int i = 0;

  if (val == 0) {
    write_str(file, "0");
    return;
  }

  while (val > 0) {
    buf[i++] = '0' + (val % 10);
    val /= 10;
  }

  // Write in reverse order
  while (i > 0) {
    DWORD written = 0;
    WriteFile(file, &buf[--i], 1, &written, nullptr);
  }
}

// Helper: write a 64-bit unsigned integer as decimal
static void write_uint64(HANDLE file, unsigned long long val) {
  char buf[32];
  int i = 0;

  if (val == 0) {
    write_str(file, "0");
    return;
  }

  while (val > 0) {
    buf[i++] = '0' + (val % 10);
    val /= 10;
  }

  // Write in reverse order
  while (i > 0) {
    DWORD written = 0;
    WriteFile(file, &buf[--i], 1, &written, nullptr);
  }
}

// Helper: write a pointer as hex
static void write_hex_address(HANDLE file, void* addr) {
  const char hex_chars[] = "0123456789abcdef";
  char buf[20];  // "0x" + 16 hex digits + "\n"
  int idx = 0;

  buf[idx++] = '0';
  buf[idx++] = 'x';

  unsigned long long val = (unsigned long long)addr;
  // Write 16 hex digits (even if leading zeros)
  for (int shift = 60; shift >= 0; shift -= 4) {
    buf[idx++] = hex_chars[(val >> shift) & 0xf];
  }

  buf[idx++] = '\n';

  DWORD written = 0;
  WriteFile(file, buf, idx, &written, nullptr);
}

// Helper: write loaded modules information
static void write_modules_windows(HANDLE file) {
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }

  write_str(file, "\nLoaded Modules:\n");

  // Create snapshot of all modules in current process
  HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId()
  );

  if (snapshot == INVALID_HANDLE_VALUE) {
    write_str(file, "[ERROR: Failed to create module snapshot]\n");
    return;
  }

  MODULEENTRY32 me;
  me.dwSize = sizeof(me);

  if (!Module32First(snapshot, &me)) {
    write_str(file, "[ERROR: Failed to enumerate modules]\n");
    CloseHandle(snapshot);
    return;
  }

  // Iterate through all modules
  do {
    // Write base address
    write_str(file, "0x");
    const char hex_chars[] = "0123456789abcdef";
    char addr_buf[16];
    int addr_idx = 0;

    unsigned long long base_addr = (unsigned long long)me.modBaseAddr;
    for (int shift = 60; shift >= 0; shift -= 4) {
      addr_buf[addr_idx++] = hex_chars[(base_addr >> shift) & 0xf];
    }
    DWORD written = 0;
    WriteFile(file, addr_buf, addr_idx, &written, nullptr);

    write_str(file, "-0x");

    // Write end address (base + size)
    addr_idx = 0;
    unsigned long long end_addr = base_addr + me.modBaseSize;
    for (int shift = 60; shift >= 0; shift -= 4) {
      addr_buf[addr_idx++] = hex_chars[(end_addr >> shift) & 0xf];
    }
    WriteFile(file, addr_buf, addr_idx, &written, nullptr);

    // Write module path
    write_str(file, " ");
    write_str(file, me.szExePath);
    write_str(file, "\n");

  } while (Module32Next(snapshot, &me));

  CloseHandle(snapshot);
}

// Exception filter - top-level crash handler
static LONG WINAPI crash_exception_filter(EXCEPTION_POINTERS* exinfo) {
  HANDLE file = s_crash_file;

  if (file == INVALID_HANDLE_VALUE) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  // Write crash report header
  write_str(file, "=== Datadog SDK Crash Report ===\n");
  write_str(file, "Exception: ");

  // Write exception code
  DWORD code = exinfo->ExceptionRecord->ExceptionCode;
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
      write_str(file, "UNKNOWN (0x");
      write_hex_address(file, (void*)(unsigned long long)code);
      write_str(file, ")");
      break;
  }
  write_str(file, "\n");

  write_str(file, "Exception code: 0x");
  char hex_buf[16];
  int hex_idx = 0;
  const char hex_chars[] = "0123456789ABCDEF";
  for (int shift = 28; shift >= 0; shift -= 4) {
    hex_buf[hex_idx++] = hex_chars[(code >> shift) & 0xf];
  }
  DWORD written = 0;
  WriteFile(file, hex_buf, hex_idx, &written, nullptr);
  write_str(file, "\n");

  write_str(file, "Exception address: ");
  write_hex_address(file, exinfo->ExceptionRecord->ExceptionAddress);

  write_str(file, "Exception flags: 0x");
  DWORD flags = exinfo->ExceptionRecord->ExceptionFlags;
  hex_idx = 0;
  for (int shift = 28; shift >= 0; shift -= 4) {
    hex_buf[hex_idx++] = hex_chars[(flags >> shift) & 0xf];
  }
  WriteFile(file, hex_buf, hex_idx, &written, nullptr);
  write_str(file, "\n");

  write_str(file, "PID: ");
  write_uint(file, GetCurrentProcessId());
  write_str(file, "\n");

  write_str(file, "TID: ");
  write_uint(file, GetCurrentThreadId());
  write_str(file, "\n");

  // Timestamp (Unix epoch format for consistency with POSIX implementation)
  time_t now = time(nullptr);
  write_str(file, "Timestamp: ");
  write_uint64(file, now);
  write_str(file, "\n");

  // Capture stack trace using RtlCaptureStackBackTrace
  write_str(file, "\nStack trace (raw addresses):\n");

  const int max_frames = 128;
  void* stack[max_frames];

  // Skip the first frame (this handler itself)
  USHORT frames = RtlCaptureStackBackTrace(1, max_frames, stack, nullptr);

  for (USHORT i = 0; i < frames; i++) {
    write_hex_address(file, stack[i]);
  }

  // Write loaded modules information
  write_modules_windows(file);

  write_str(file, "\n=== End of crash report ===\n");

  // Close file before terminating
  CloseHandle(file);
  s_crash_file = INVALID_HANDLE_VALUE;

  // Terminate process
  return EXCEPTION_EXECUTE_HANDLER;
}

class InProcessCrashHandler final : public ICrashHandler {
 public:
  explicit InProcessCrashHandler(impl::DiagnosticLogger& logger) : _logger(logger) {}

  bool Initialize() override {
    // 1. Create .crashes directory
    // Using CreateDirectoryA (narrow API). May fail with non-ASCII working directory.
    if (!CreateDirectoryA(".crashes", nullptr)) {
      DWORD err = GetLastError();
      if (err != ERROR_ALREADY_EXISTS) {
        _logger.Error("Failed to create .crashes directory");
        return false;
      }
    }

    // 2. Format crash filename with timestamp and PID
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
        (unsigned long)GetCurrentProcessId()
    );

    // 3. Pre-open crash file
    s_crash_file = CreateFileA(
        s_crash_filename,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (s_crash_file == INVALID_HANDLE_VALUE) {
      _logger.Error("Failed to create crash file");
      return false;
    }

    // 4. Install exception filter
    // Note: SetUnhandledExceptionFilter can be overridden by other code.
    // A production implementation should consider Vectored Exception Handlers (VEH).
    s_old_filter = SetUnhandledExceptionFilter(crash_exception_filter);

    _logger.Debug("In-process crash handler initialized successfully");
    return true;
  }

  void Shutdown() override {
    // Restore old exception filter
    if (s_old_filter != nullptr) {
      SetUnhandledExceptionFilter(s_old_filter);
      s_old_filter = nullptr;
    }

    // Close crash file
    if (s_crash_file != INVALID_HANDLE_VALUE) {
      CloseHandle(s_crash_file);
      s_crash_file = INVALID_HANDLE_VALUE;
    }
  }

 private:
  impl::DiagnosticLogger& _logger;
};

std::unique_ptr<ICrashHandler> CrashHandler::Init(
    impl::DiagnosticLogger& logger, std::string_view handler_exe_path
) {
  (void)handler_exe_path;
  return std::make_unique<InProcessCrashHandler>(logger);
}

}  // namespace datadog::platform
