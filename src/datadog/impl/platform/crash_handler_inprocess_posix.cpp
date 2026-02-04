// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// Expose ucontext routines by explicitly requesting POSIX.1-2008 / XPG7. This is a
// valid feature test macro, not a redefinition of a reserved identifier.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _XOPEN_SOURCE 700

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstring>
#include <ctime>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#endif

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/crash_handler.hpp"

namespace datadog::platform {

// This file implements an async-signal-safe crash handler that must use low-level
// C-style operations and avoid modern C++ constructs. The following linter checks
// are disabled because they conflict with async-signal-safety requirements:
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// NOLINTBEGIN(readability-braces-around-statements)
// NOLINTBEGIN(google-readability-braces-around-statements)
// NOLINTBEGIN(google-runtime-int)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTBEGIN(cppcoreguidelines-narrowing-conversions)
// NOLINTBEGIN(bugprone-narrowing-conversions)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast)
// NOLINTBEGIN(google-readability-casting)
// NOLINTBEGIN(performance-no-int-to-ptr)
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// NOLINTBEGIN(cppcoreguidelines-no-malloc)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(readability-use-std-min-max)

// BEHAVIOR: Only the first crash per process is captured.
// Subsequent crashes (if handler doesn't exit) use OS default handler.

// Signal-safe global state required for async-signal-safe crash handler
static volatile sig_atomic_t s_crash_fd = -1;
static char s_crash_filename[256];
static struct sigaction s_old_sigsegv;
static struct sigaction s_old_sigbus;
static struct sigaction s_old_sigill;
static struct sigaction s_old_sigfpe;
static struct sigaction s_old_sigabrt;
static void* s_sigalt_stack = nullptr;

// Async-signal-safe helper: write a string literal
static void write_str(int fd, const char* str) {
  size_t len = 0;
  while (str[len]) len++;
  ssize_t result = write(fd, str, len);
  (void)result;  // Intentionally ignore - crash handler cannot handle errors
}

// Async-signal-safe helper: write an unsigned integer as decimal
static void write_uint(int fd, unsigned long val) {
  char buf[32];
  int i = 0;

  if (val == 0) {
    ssize_t result = write(fd, "0", 1);
    (void)result;  // Intentionally ignore - crash handler cannot handle errors
    return;
  }

  while (val > 0) {
    buf[i++] = '0' + (val % 10);
    val /= 10;
  }

  // Write in reverse order
  while (i > 0) {
    ssize_t result = write(fd, &buf[--i], 1);
    (void)result;  // Intentionally ignore - crash handler cannot handle errors
  }
}

// Async-signal-safe helper: write a pointer as hex
static void write_hex_address(int fd, void* addr) {
  const char hex_chars[] = "0123456789abcdef";
  char buf[20];  // "0x" + 16 hex digits + "\n"
  int idx = 0;

  buf[idx++] = '0';
  buf[idx++] = 'x';

  unsigned long val = (unsigned long)addr;
  // Write 16 hex digits (even if leading zeros)
  for (int shift = 60; shift >= 0; shift -= 4) {
    buf[idx++] = hex_chars[(val >> shift) & 0xf];
  }

  buf[idx++] = '\n';
  ssize_t result = write(fd, buf, idx);
  (void)result;  // Intentionally ignore - crash handler cannot handle errors
}

// Async-signal-safe stack trace writer using frame-pointer walking
static void write_stack_trace(int fd, void* frame_pointer) {
  write_str(fd, "\nStack trace (raw addresses):\n");

  void* fp = frame_pointer;
  const int max_frames = 128;

  for (int i = 0; i < max_frames && fp != nullptr; i++) {
    // Validate frame pointer looks reasonable
    // Must be aligned and not obviously invalid
    if ((unsigned long)fp < 0x1000 || ((unsigned long)fp & 0x7) != 0) {
      break;
    }

    void** frame = (void**)fp;

    // frame[0] = previous frame pointer
    // frame[1] = return address
    void* ret_addr = frame[1];

    if (ret_addr == nullptr) {
      break;
    }

    write_hex_address(fd, ret_addr);

    fp = frame[0];  // Move to next frame

    // Stop if next frame pointer is invalid
    if (fp != nullptr && (unsigned long)fp <= (unsigned long)frame) {
      break;  // Prevent backwards or circular references
    }
  }
}

#ifdef __APPLE__
// Async-signal-safe helper: write a pointer as hex without newline
static void write_hex_no_newline(int fd, unsigned long val) {
  const char hex_chars[] = "0123456789abcdef";
  char buf[18];  // "0x" + 16 hex digits
  int idx = 0;

  buf[idx++] = '0';
  buf[idx++] = 'x';

  // Write 16 hex digits (even if leading zeros)
  for (int shift = 60; shift >= 0; shift -= 4) {
    buf[idx++] = hex_chars[(val >> shift) & 0xf];
  }

  ssize_t result = write(fd, buf, idx);
  (void)result;  // Intentionally ignore - crash handler cannot handle errors
}

// NOTE: dyld APIs used here are not documented as async-signal-safe.
// However, they are simple getters reading internal data structures
// and are widely used in practice for crash reporting. Use with caution.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void write_modules_macos(int fd) {
  write_str(fd, "\nLoaded Modules:\n");

  uint32_t image_count = _dyld_image_count();

  for (uint32_t i = 0; i < image_count; i++) {
    const char* image_name = _dyld_get_image_name(i);
    const struct mach_header* header = _dyld_get_image_header(i);

    if (!header || !image_name) {
      continue;
    }

    // Calculate the base address and module size by parsing load commands
    unsigned long base_addr = (unsigned long)header;

    // Find the range of vmaddr values in segments
    // Note: vmaddr is relative to the module's preferred load address
    // The actual load address is base_addr (after ASLR slide)
    unsigned long min_vmaddr = 0xFFFFFFFFFFFFFFFF;
    unsigned long max_vmaddr_end = 0;

    // Determine if this is 64-bit or 32-bit mach_header
    bool is_64bit = (header->magic == MH_MAGIC_64 || header->magic == MH_CIGAM_64);

    const uint8_t* cmd_ptr = nullptr;
    uint32_t ncmds = 0;

    if (is_64bit) {
      const struct mach_header_64* header64 = (const struct mach_header_64*)header;
      cmd_ptr = (const uint8_t*)(header64 + 1);
      ncmds = header64->ncmds;
    } else {
      cmd_ptr = (const uint8_t*)(header + 1);
      ncmds = header->ncmds;
    }

    // Iterate through load commands to find the address range
    // Skip __PAGEZERO (vmaddr=0, large vmsize) - it's not actually mapped
    for (uint32_t cmd_idx = 0; cmd_idx < ncmds; cmd_idx++) {
      const struct load_command* cmd = (const struct load_command*)cmd_ptr;

      if (cmd->cmd == LC_SEGMENT_64) {
        const struct segment_command_64* seg = (const struct segment_command_64*)cmd;
        // Skip __PAGEZERO - check if segment name is "__PAGEZERO"
        bool is_pagezero =
            (seg->segname[0] == '_' && seg->segname[1] == '_' &&
             seg->segname[2] == 'P' && seg->segname[3] == 'A' &&
             seg->segname[4] == 'G' && seg->segname[5] == 'E' &&
             seg->segname[6] == 'Z' && seg->segname[7] == 'E' &&
             seg->segname[8] == 'R' && seg->segname[9] == 'O');
        if (!is_pagezero && seg->vmsize > 0) {
          if (seg->vmaddr < min_vmaddr) {
            min_vmaddr = seg->vmaddr;
          }
          unsigned long seg_end = seg->vmaddr + seg->vmsize;
          if (seg_end > max_vmaddr_end) {
            max_vmaddr_end = seg_end;
          }
        }
      } else if (cmd->cmd == LC_SEGMENT) {
        const struct segment_command* seg = (const struct segment_command*)cmd;
        // Skip __PAGEZERO
        bool is_pagezero =
            (seg->segname[0] == '_' && seg->segname[1] == '_' &&
             seg->segname[2] == 'P' && seg->segname[3] == 'A' &&
             seg->segname[4] == 'G' && seg->segname[5] == 'E' &&
             seg->segname[6] == 'Z' && seg->segname[7] == 'E' &&
             seg->segname[8] == 'R' && seg->segname[9] == 'O');
        if (!is_pagezero && seg->vmsize > 0) {
          if (seg->vmaddr < min_vmaddr) {
            min_vmaddr = seg->vmaddr;
          }
          unsigned long seg_end = seg->vmaddr + seg->vmsize;
          if (seg_end > max_vmaddr_end) {
            max_vmaddr_end = seg_end;
          }
        }
      }

      cmd_ptr += cmd->cmdsize;
    }

    // Calculate actual load addresses
    // base_addr is already the actual load address (after ASLR)
    // The module size is the span from min to max vmaddr
    unsigned long actual_start = base_addr;
    unsigned long module_size = max_vmaddr_end - min_vmaddr;
    unsigned long actual_end = base_addr + module_size;

    // Write the module information in format: 0x<base>-0x<end> <path>
    write_hex_no_newline(fd, actual_start);
    write_str(fd, "-");
    write_hex_no_newline(fd, actual_end);
    write_str(fd, " ");
    write_str(fd, image_name);
    write_str(fd, "\n");
  }
}
#endif

// Signal handler - MUST be async-signal-safe
static void crash_signal_handler(int sig, siginfo_t* info, void* ucontext_raw) {
  int fd = s_crash_fd;

  if (fd < 0) {
    _exit(128 + sig);
  }

  // Write crash report header
  write_str(fd, "=== Datadog SDK Crash Report ===\n");
  write_str(fd, "Signal: ");

  switch (sig) {
    case SIGSEGV:
      write_str(fd, "SIGSEGV");
      break;
    case SIGBUS:
      write_str(fd, "SIGBUS");
      break;
    case SIGILL:
      write_str(fd, "SIGILL");
      break;
    case SIGFPE:
      write_str(fd, "SIGFPE");
      break;
    case SIGABRT:
      write_str(fd, "SIGABRT");
      break;
    default:
      write_uint(fd, sig);
      break;
  }
  write_str(fd, "\n");

  write_str(fd, "Signal code: ");
  write_uint(fd, info->si_code);
  write_str(fd, "\n");

  write_str(fd, "Fault address: ");
  write_hex_address(fd, info->si_addr);

  write_str(fd, "PID: ");
  write_uint(fd, getpid());
  write_str(fd, "\n");

  write_str(fd, "TID: ");
  write_uint(fd, (unsigned long)pthread_self());
  write_str(fd, "\n");

  // Timestamp (simple format, async-signal-safe)
  time_t now = time(nullptr);
  write_str(fd, "Timestamp: ");
  write_uint(fd, now);
  write_str(fd, "\n");

  // Extract frame pointer from ucontext
  void* fp = nullptr;
  ucontext_t* uc = (ucontext_t*)ucontext_raw;

#if defined(__APPLE__)
// macOS ucontext structure
#ifdef __x86_64__
  fp = (void*)uc->uc_mcontext->__ss.__rbp;
#elif defined(__aarch64__)
  fp = (void*)uc->uc_mcontext->__ss.__fp;
#else
#error "Unsupported architecture for in-process crash handler on macOS"
#endif
#else
// Linux ucontext structure
#ifdef __x86_64__
  fp = (void*)uc->uc_mcontext.gregs[REG_RBP];
#elif defined(__aarch64__)
  fp = (void*)uc->uc_mcontext.regs[29];  // x29 = FP on ARM64
#else
#error "Unsupported architecture for in-process crash handler on Linux"
#endif
#endif

  // Write stack trace
  write_stack_trace(fd, fp);

#ifdef __APPLE__
  // Write loaded modules (macOS only for Phase 1)
  write_modules_macos(fd);
#endif

  write_str(fd, "\n=== End of crash report ===\n");

  // Do not close fd - process is about to die anyway
  // Exit immediately
  _exit(128 + sig);
}

class InProcessCrashHandler final : public ICrashHandler {
 public:
  explicit InProcessCrashHandler(impl::DiagnosticLogger& logger) : _logger(logger) {}

  bool Initialize() override {
    // 1. Create .crashes directory
    if (mkdir(".crashes", 0755) < 0 && errno != EEXIST) {
      _logger.Error("Failed to create .crashes directory");
      return false;
    }

    // 2. Format crash filename with timestamp and PID
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    snprintf(
        s_crash_filename,
        sizeof(s_crash_filename),
        ".crashes/crash_%04d%02d%02d_%02d%02d%02d_%d.txt",
        tm_buf.tm_year + 1900,
        tm_buf.tm_mon + 1,
        tm_buf.tm_mday,
        tm_buf.tm_hour,
        tm_buf.tm_min,
        tm_buf.tm_sec,
        getpid()
    );

    // 3. Pre-open crash file
    s_crash_fd = open(s_crash_filename, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (s_crash_fd < 0) {
      _logger.Error("Failed to open crash file for writing");
      return false;
    }

    // 4. Allocate alternate signal stack
    const size_t stack_size = SIGSTKSZ * 2;  // 16KB on most systems
    s_sigalt_stack = malloc(stack_size);
    if (!s_sigalt_stack) {
      close(s_crash_fd);
      s_crash_fd = -1;
      _logger.Error("Failed to allocate signal stack");
      return false;
    }

    // 5. Install alternate signal stack
    stack_t ss;
    ss.ss_sp = s_sigalt_stack;
    ss.ss_size = stack_size;
    ss.ss_flags = 0;

    if (sigaltstack(&ss, nullptr) < 0) {
      free(s_sigalt_stack);
      s_sigalt_stack = nullptr;
      close(s_crash_fd);
      s_crash_fd = -1;
      _logger.Error("Failed to install signal stack");
      return false;
    }

    // 6. Register signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    sigemptyset(&sa.sa_mask);

    // Try to register all signal handlers. If any fail, clean up and return false.
    bool success = true;
    if (sigaction(SIGSEGV, &sa, &s_old_sigsegv) < 0) {
      success = false;
    } else if (sigaction(SIGBUS, &sa, &s_old_sigbus) < 0) {
      sigaction(SIGSEGV, &s_old_sigsegv, nullptr);
      success = false;
    } else if (sigaction(SIGILL, &sa, &s_old_sigill) < 0) {
      sigaction(SIGBUS, &s_old_sigbus, nullptr);
      sigaction(SIGSEGV, &s_old_sigsegv, nullptr);
      success = false;
    } else if (sigaction(SIGFPE, &sa, &s_old_sigfpe) < 0) {
      sigaction(SIGILL, &s_old_sigill, nullptr);
      sigaction(SIGBUS, &s_old_sigbus, nullptr);
      sigaction(SIGSEGV, &s_old_sigsegv, nullptr);
      success = false;
    } else if (sigaction(SIGABRT, &sa, &s_old_sigabrt) < 0) {
      sigaction(SIGFPE, &s_old_sigfpe, nullptr);
      sigaction(SIGILL, &s_old_sigill, nullptr);
      sigaction(SIGBUS, &s_old_sigbus, nullptr);
      sigaction(SIGSEGV, &s_old_sigsegv, nullptr);
      success = false;
    }

    if (!success) {
      // Clean up resources
      free(s_sigalt_stack);
      s_sigalt_stack = nullptr;
      close(s_crash_fd);
      s_crash_fd = -1;

      _logger.Error("Failed to install signal handlers");
      return false;
    }

    // Success
    _logger.Debug("In-process crash handler initialized successfully");
    return true;
  }

  void Shutdown() override {
    // Restore old signal handlers
    sigaction(SIGABRT, &s_old_sigabrt, nullptr);
    sigaction(SIGFPE, &s_old_sigfpe, nullptr);
    sigaction(SIGILL, &s_old_sigill, nullptr);
    sigaction(SIGBUS, &s_old_sigbus, nullptr);
    sigaction(SIGSEGV, &s_old_sigsegv, nullptr);

    // Close crash file
    if (s_crash_fd >= 0) {
      close(s_crash_fd);
      s_crash_fd = -1;
    }

    // Do not free sigaltstack - unsafe if currently in use
    // Memory leak is acceptable on shutdown
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

// NOLINTEND(readability-use-std-min-max)
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-no-malloc)
// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-pro-type-member-init)
// NOLINTEND(performance-no-int-to-ptr)
// NOLINTEND(google-readability-casting)
// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTEND(bugprone-narrowing-conversions)
// NOLINTEND(cppcoreguidelines-narrowing-conversions)
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTEND(google-runtime-int)
// NOLINTEND(google-readability-braces-around-statements)
// NOLINTEND(readability-braces-around-statements)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace datadog::platform
