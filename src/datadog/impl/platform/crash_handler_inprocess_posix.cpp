// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// Expose ucontext et al. by explicitly requesting POSIX.1-2008/XPG7-compliant APIs.
// This is a valid feature test macro, not a redefinition of a reserved identifier. We
// use ucontext_t to extract CPU register state (frame pointer) from crashed threads.
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _XOPEN_SOURCE 700

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#include <cinttypes>
#include <cstring>
#include <ctime>

// We only officially support 64-bit architectures; this code may need to be revisited
// if we add legacy 32-bit support
static_assert(
    sizeof(uint64_t) == sizeof(uintptr_t),
    "Unexpected uintptr_t size; POSIX crash handler assumes LP64"
);

// macOS binaries use Mach-O format, and shared libraries are loaded via dyld: we use
// _dyld_get_image_header(), mach_header, et al. in order to enumerate and inspect the
// set of loaded modules
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
// NOLINTBEGIN(cppcoreguidelines-no-malloc)
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// NOLINTBEGIN(readability-use-std-min-max)

// BEHAVIOR: Only the first crash per process is captured.
// Subsequent crashes (if handler doesn't exit) use OS default handler.

// === Global state ===
// - Preemptively initialized in InProcessCrashHandler::Initialize(), when it's still
//   safe to allocate, call ordinary system functions, etc.
// - Subsequently used during crash, where we're constrained to signal-safe routines

// Pre-opened file descriptor for crash report file to be written
static volatile sig_atomic_t s_crash_fd = -1;
static char s_crash_filename[256];  // Path to crash report file

// Original signal handlers to restore on shutdown
static struct sigaction s_old_sigsegv;
static struct sigaction s_old_sigbus;
static struct sigaction s_old_sigill;
static struct sigaction s_old_sigfpe;
static struct sigaction s_old_sigabrt;

// Alternate stack for signal handler, to prevent stack overflow if crash was caused by
// stack exhaustion
static void* s_sigalt_stack = nullptr;

// === Signal-safe helpers for string parsing/formatting and file I/O ===

/**
 * Writes a null-terminated string to the given file descriptor, using only signal-safe
 * functions.
 */
static void write_str(int fd, const char* str) {
  // Compute string length manually, as, strlen is not guaranteed to be signal-safe
  size_t len = 0;
  while (str[len]) {
    len++;
  }

  // Write to the open file descriptor, ignoring the result since the crash handler has
  // no recovery path for I/O errors
  ssize_t result = write(fd, str, len);
  (void)result;
}

/**
 * Writes a literal newline character to the given file descriptor.
 */
static void write_newline(int fd) {
  ssize_t result = write(fd, "\n", 1);
  (void)result;
}

/**
 * Writes an unsigned, 64-bit integer to the given file descriptor as an ASCII string,
 * in decimal format, using only signal-safe functions.
 */
static void write_uint(int fd, uint64_t val) {
  // Early-out for 0: write literal '0', ignoring result
  if (val == 0) {
    ssize_t result = write(fd, "0", 1);
    (void)result;
    return;
  }

  // Establish a buffer large enough to contain the max possible uint
  char buf[32];
  int i = 0;

  // sprintf/snprintf are not signal-safe: manually accumulate the digits of our decimal
  // value into the buffer, from least-significant to most-significant
  while (val > 0) {
    buf[i++] = static_cast<char>('0' + (val % 10));
    val /= 10;
  }

  // Iterate backwards to write each of our digits into the file in reverse, ignoring
  // the result since the crash handler has no recovery path for I/O errors
  while (i > 0) {
    ssize_t result = write(fd, &buf[--i], 1);
    (void)result;  // Intentionally ignore - crash handler cannot handle errors
  }
}

/**
 * Writes an address to the given file descriptor in lowercase hex format, prefixed with
 * '0x', always padded with leading zeroes to 16 hex digits (64-bit addresses), using
 * only signal-safe functions.
 */
static void write_hex_address(int fd, uintptr_t addr) {
  // Establish a constant lookup table of hex digits, and a buffer large enough to
  // contain "0x" + 16 hex digits
  const char hex_chars[] = "0123456789abcdef";
  char buf[18];

  // Write the 2-byte prefix to the buffer
  int i = 0;
  buf[i++] = '0';
  buf[i++] = 'x';

  // sprintf/snprintf are not signal-safe: manually convert to hex by starting at the
  // top nibble (bits 60, 61, 62, 63), masking those 4 bytes to resolve the most
  // significant hex digit, then progressing forward toward less-significant digits,
  // stepping down by 4 bytes each time
  for (int shift = 60; shift >= 0; shift -= 4) {
    // Write the appropriate hex digit into the next position in our buffer
    buf[i++] = hex_chars[(addr >> shift) & 0xf];
  }

  // Write the 18 bytes from our buffer to the open file descriptor, ignoring the result
  // since the crash handler has no recovery path for I/O errors
  ssize_t result = write(fd, buf, i);
  (void)result;
}

// === Stack unwinding ===
// - We write stack frames to the crash report file as we iterate through the stack.
// - For simplicity and maximum signal-safety, we currently walk the call stack by
//   manually following the frame pointer chain.
// - This approach requires binaries to be compiled with frame pointers intact: release
//   builds of the client application may require `-fno-omit-frame-pointer` (disabling
//   an optimization that uses RBP/FP as a general-purpose register) in order to produce
//   accurate callstacks.
// - This caveat may be avoidable in the future with careful use of libunwind to
//   reconstruct callstacks based on .eh_frame / DWARF CFI metadata.

/**
 * Writes a header indicating the start of the stack trace, followed by the instruction
 * pointer value indicating where the crash occurred, followed by the return addresses
 * stored in each subsequent stack frame.
 *
 * Traverses the stack, starting from the frame_pointer address that's currently loaded
 * in the RBP/FP register, writing the return address from each stack frame into the
 * crash report file on its own line, hex-encoded.
 */
static void write_stack_trace(int fd, void* instruction_pointer, void* frame_pointer) {
  // The topmost (most-recently-pushed) frame in our callstack represents the function
  // in which the crash occurred: the value of the instruction pointer / program counter
  // at the time of the crash indicates exactly where the crash occurred
  write_hex_address(fd, reinterpret_cast<uintptr_t>(instruction_pointer));
  write_newline(fd);

  // Stop after 128 frames to bound execution time and prevent runaway traversal
  const int max_frames = 128;

  // With fp as the address of the stack frame we're currently examining, iterate until
  // we exceed max_frames, reach the end of the callstack, or detect an invalid frame
  // pointer value or signs of stack corruption
  void* fp = frame_pointer;
  for (int i = 0; i < max_frames && fp != nullptr; i++) {
    // If the frame pointer value is not 8-byte aligned, it's not a valid frame pointer
    if ((reinterpret_cast<uintptr_t>(fp) & 0x7) != 0) {
      break;
    }

    // If the frame pointer value refers to an address in the first page of virtual
    // memory, it's not a real frame pointer, or it's been corrupted such that
    // dereferencing it would cause another segfault within this crash handler
    if (reinterpret_cast<uintptr_t>(fp) < 0x1000) {
      break;
    }

    // In any given stack frame:
    // - [fp+0] = pointer to caller's stack frame (the previous frame in the call chain)
    // - [fp+8] = return address (instruction pointer where caller will resume)
    void** frame = reinterpret_cast<void**>(fp);

    // Treating these first 16 bytes of the stack frame as an array of 2 addresses,
    // frame[1] gives us the return address: if that value is 0x00000000, we've reached
    // the end of the call stack _or_ the stack is corrupted
    void* ret_addr = frame[1];
    if (ret_addr == nullptr) {
      break;
    }

    // Write the current stack frame's return address to the crash report file: this
    // address points to the instruction to be executed after this function returns
    // (immediately following the call that created this frame). Symbolication tools
    // will adjust to resolve the actual call site.
    write_hex_address(fd, reinterpret_cast<uintptr_t>(ret_addr));
    write_newline(fd);

    // Reading frame[0] into fp (effectively dereferencing fp) moves to the next frame
    fp = frame[0];

    // The stack grows downward: each frame pushed onto the stack has an incrementally
    // lower address. Since we're iterating in the opposite direction, our new stack
    // frame `fp` must have a _higher_ address in stack memory than the frame we just
    // examined (`frame`). If it it's lower or unchanged, break to prevent backwards
    // traversal or circular references.
    if (fp != nullptr &&
        reinterpret_cast<uintptr_t>(fp) <= reinterpret_cast<uintptr_t>(frame)) {
      break;
    }
  }
}

// === Enumeration of loaded modules ===
// - Uses dyld APIs on macOS; reads from /proc/self/maps on Linux

/**
 * Resolves a list of all modules that are loaded in the current process, and writes
 * them to the given file descriptor, in the format:
 *
 * <start-address>-<end-address> <binary-path>
 *
 * Where addresses are encoded as '0x'-prefixed, lowercase hex values zero-padded to 16
 * bytes, and binary path is the full path to the executable or library file.
 */
static void write_modules(int fd);

#ifdef __APPLE__
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void write_modules(int fd) {
  // Use dyld APIs to retrieve information about loaded modules: these functions are
  // not officially documented as async-signal-safe, but they're widely used in crash
  // reporters, and they read structures already present in read-only memory
  uint32_t image_count = _dyld_image_count();
  for (uint32_t i = 0; i < image_count; i++) {
    // Each module's Mach-O header is followed by load commands describing memory
    // segments: we can parse these segments to determine the module's memory footprint
    const char* image_name = _dyld_get_image_name(i);
    const struct mach_header* header = _dyld_get_image_header(i);
    if (!header || !image_name) {
      continue;
    }

    // base_addr is the actual runtime address of the module, after ASLR. By contrast, a
    // segment's vmaddr indicates its preferred virtual address before ASLR.
    // (base address == vmaddr of first segment + ASLR slide)
    uintptr_t base_addr = reinterpret_cast<uintptr_t>(header);

    // The Mach header sits in front of a series of load commands: the first address
    // after the end of the header is the address of the first load_command, and we can
    // read the header itself to determine how many load commands there are
    const uint8_t* cmd_ptr = nullptr;
    uint32_t ncmds = 0;
    if (header->magic == MH_MAGIC_64 || header->magic == MH_CIGAM_64) {
      // Mach header is 64-bit; increment by sizeof(mach_header_64)
      const auto* header_64 = reinterpret_cast<const struct mach_header_64*>(header);
      cmd_ptr = reinterpret_cast<const uint8_t*>(header_64 + 1);
      ncmds = header_64->ncmds;
    } else {
      // Mach header is 32-bit; increment by sizeof(mach_header)
      cmd_ptr = reinterpret_cast<const uint8_t*>(header + 1);
      ncmds = header->ncmds;
    }

    // We iterate through load commands (LC_SEGMENT/LC_SEGMENT_64) to find the span of
    // addresses occupied by the module in memory. Each load command describes a
    // memory segment, with a segname like '__PAGEZERO', '__TEXT', '__DATA', etc.
    // __PAGEZERO is an unmapped zero page spanning the first 0x100000000 bytes, so we
    // skip it and only consider pages that actually map the contents of the binary into
    // process memory. For all such pages, we accumulate the lowest-seen vmaddr and the
    // highest-seen vmaddr_end (vmaddr + vmsize), giving us the range in virtual memory
    // where the module is mapped, prior to ASLR slide.
    uintptr_t min_vmaddr = 0xFFFFFFFFFFFFFFFF;
    uintptr_t max_vmaddr_end = 0;
    for (uint32_t cmd_idx = 0; cmd_idx < ncmds; cmd_idx++) {
      // If the current load comand describes a valid segment other than __PAGEZERO,
      // compute its vmaddr range and update min_vmaddr / max_vmaddr_end
      const auto* cmd = reinterpret_cast<const struct load_command*>(cmd_ptr);
      if (cmd->cmd == LC_SEGMENT_64) {
        // If Mach-O header was 64-bit, segments will be described by segment_command_64
        // structs using uint64_t addresses
        const auto* seg = reinterpret_cast<const struct segment_command_64*>(cmd);
        const bool is_pagezero =
            (seg->segname[0] == '_' && seg->segname[1] == '_' &&
             seg->segname[2] == 'P' && seg->segname[3] == 'A' &&
             seg->segname[4] == 'G' && seg->segname[5] == 'E' &&
             seg->segname[6] == 'Z' && seg->segname[7] == 'E' &&
             seg->segname[8] == 'R' && seg->segname[9] == 'O');
        if (!is_pagezero && seg->vmsize > 0) {
          if (seg->vmaddr < min_vmaddr) {
            min_vmaddr = seg->vmaddr;
          }
          uintptr_t seg_end = seg->vmaddr + seg->vmsize;
          if (seg_end > max_vmaddr_end) {
            max_vmaddr_end = seg_end;
          }
        }
      } else if (cmd->cmd == LC_SEGMENT) {
        // If Mach-O header was 32-bit, segments will be described by segment_command
        // structs using uint32_t addresses
        const auto* seg = reinterpret_cast<const struct segment_command*>(cmd);
        const bool is_pagezero =
            (seg->segname[0] == '_' && seg->segname[1] == '_' &&
             seg->segname[2] == 'P' && seg->segname[3] == 'A' &&
             seg->segname[4] == 'G' && seg->segname[5] == 'E' &&
             seg->segname[6] == 'Z' && seg->segname[7] == 'E' &&
             seg->segname[8] == 'R' && seg->segname[9] == 'O');
        if (!is_pagezero && seg->vmsize > 0) {
          if (seg->vmaddr < min_vmaddr) {
            min_vmaddr = seg->vmaddr;
          }
          uintptr_t seg_end = seg->vmaddr + seg->vmsize;
          if (seg_end > max_vmaddr_end) {
            max_vmaddr_end = seg_end;
          }
        }
      }

      // Advance to the next load command
      cmd_ptr += cmd->cmdsize;
    }

    // Having found the extent of vmaddr ranges for all segments, we can compute the
    // size of the module
    const uintptr_t module_size = max_vmaddr_end - min_vmaddr;

    // If using ASLR, dyld has computed a randomized ASLR slide offset for this module,
    // such that base_addr (the actual load address of the module) is shifted from the
    // preferred vmaddr by that ASLR slide: adding the module size to base_addr gives us
    // the actual end address of the module
    const uintptr_t actual_start = base_addr;
    const uintptr_t actual_end = base_addr + module_size;

    // Write the module information in format: 0x<base>-0x<end> <path>
    write_hex_address(fd, actual_start);
    write_str(fd, "-");
    write_hex_address(fd, actual_end);
    write_str(fd, " ");
    write_str(fd, image_name);
    write_newline(fd);
  }
}
#endif  // __APPLE__

#ifdef __linux__
/**
 * Converts the given hex digit to a decimal value in the range [0,15]. Returns -1 if c
 * is not a hex digit.
 */
static int hex_char_to_int(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/**
 * Converts a hexadecimal string (e.g. "7ffae0000") to an unsigned integer (e.g.
 * 34354364416), storing the result in out_value. Reads all contiguous hex characters
 * from `str`, stopping when it encounters any non-hex-digit character. Returns the
 * number of hex characters consumed from the string.
 */
static size_t parse_hex_address(const char* str, uintptr_t& out_value) {
  out_value = 0;
  size_t num_chars = 0;
  while (str[num_chars]) {
    int digit = hex_char_to_int(str[num_chars]);
    if (digit < 0) {
      break;
    }
    out_value = (out_value << 4) | digit;
    num_chars++;
  }
  return num_chars;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void write_modules(int fd) {
  // Open /proc/self/maps for read: this virtual file contains a line for each loaded
  // module in the current process, with the format:
  //
  // - <start-address>-<end-address> <perms> <offset> <dev> <inode> <pathname>
  // - e.g. "7ffae0000-7ffae1000 r-xp 00000000 08:02 12345"
  //
  // We only consider executable mappings (where perms contains 'x') with valid
  // pathnames, excluding pseudo-files like [vdso], [heap], [stack], etc. For each valid
  // module identified, we write its address range and path to the report file `fd` in
  // our standard format.
  int maps_fd = open("/proc/self/maps", O_RDONLY);
  if (maps_fd < 0) {
    write_str(fd, "Error: Failed to open /proc/self/maps\n");
    return;
  }

  // We can't use getline (which allocates) or fgets (which requires FILE* and is not
  // signal-safe), so we implement line-by-line parsing directly. We'll read from the
  // file chunk-by-chunk, buffering each line's contents into `line`, processing the
  // complete line once we hit a newline.
  char buffer[4096];
  char line[512];

  // line_write_pos indicates the current write position in the line buffer; preserved
  // across chunk boundaries so we properly handle partial line reads
  size_t line_write_pos = 0;

  // Continually read from the file, buffering and parsing each line, until we hit EOF
  // or a read fails
  while (true) {
    // Read the next chunk of the file, aborting on failure
    ssize_t bytes_read = read(maps_fd, buffer, sizeof(buffer));
    if (bytes_read <= 0) {
      break;
    }

    // Iterate through the bytes we just read, accumulating data into the line buffer
    // and then parsing each complete line
    for (ssize_t i = 0; i < bytes_read; i++) {
      // If the current character is anything but a newline, add it the line buffer and
      // continue
      const char c = buffer[i];
      if (c != '\n') {
        if (line_write_pos + 1 < sizeof(line)) {
          line[line_write_pos++] = c;
        } else {
          // Line exceeds expected length; all characters past sizeof(line) are silently
          // dropped
        }
        continue;
      }

      // Current character is a newline, meaning we've accumulated an entire line into
      // the line buffer: parse that line in its entirety, determine if the line
      // describes an executable module with a valid path, and write its details to the
      // output file if so

      // Terminate the line so we can safely read character-by-character without bounds
      // checks
      line[line_write_pos] = '\0';

      // Parse the module's start address
      uintptr_t start_addr = 0;
      size_t line_read_pos = parse_hex_address(line, start_addr);

      // Skip the ensuing hyphen, rejecting the line if format is unexpected
      if (line[line_read_pos] != '-') {
        line_write_pos = 0;
        continue;
      }
      line_read_pos++;

      // Parse module's end address
      uintptr_t end_addr = 0;
      line_read_pos += parse_hex_address(line + line_read_pos, end_addr);

      // Advance to the next whitespace-delimited token, which should be a 4-character
      // permission string: e.g. "rwxp", "r-xp"
      while (line[line_read_pos] == ' ') {
        line_read_pos++;
      }

      // Reject the line if it doesn't contain at least 4 more characters starting from
      // where the permission string should be
      if (line_read_pos + 4 > line_write_pos) {
        line_write_pos = 0;
        continue;
      }

      // perms[2] should be 'x' if the module is executable
      const bool is_executable = line[line_read_pos + 2] == 'x';
      line_read_pos += 4;

      // If the module is not executable, we don't need to list it in our report:
      // proceed to the next line
      if (!is_executable) {
        line_write_pos = 0;
        continue;
      }

      // The next three whitespace-delimited tokens are offset, dev, and inode: skip
      // past these values in order to get to the final token, the pathname
      int num_fields_skipped = 0;
      while (line[line_read_pos] && num_fields_skipped < 3) {
        // Advance past the next contiguous region of spaces
        while (line[line_read_pos] == ' ') {
          line_read_pos++;
        }
        // Advance past the next contiguous region of non-space characters (i.e. the
        // field value itself)
        while (line[line_read_pos] && line[line_read_pos] != ' ') {
          line_read_pos++;
        }
        // Repeat until we've skipped the desired number of fields
        num_fields_skipped++;
      }

      // Skip leading whitespace before the pathname
      while (line[line_read_pos] == ' ') {
        line_read_pos++;
      }

      // If there's no value specified for pathname, this is an anonymous mapping: skip
      // it and proceed to the next line
      if (!line[line_read_pos]) {
        line_write_pos = 0;
        continue;
      }

      // If the pathname value is labeled with a name in brackets, it's also an
      // anonymous mapping that doesn't correspond to a concrete binary file (e.g.
      // [vsdo], [vvar], [vsyscall], etc.): skip it as well
      const char* pathname = line + line_read_pos;
      if (pathname[0] == '[') {
        line_write_pos = 0;
        continue;
      }

      // We've parsed the address range and pathname of a loaded module that should be
      // included in our crash report: write that information to the output file in a
      // single line with the format `<start-address>-<end-address> <pathname>`, with
      // addresses formatted as fixed-width, '0x'-prefixed hex strings
      write_hex_address(fd, start_addr);
      write_str(fd, "-");
      write_hex_address(fd, end_addr);
      write_str(fd, " ");
      write_str(fd, pathname);
      write_newline(fd);

      // Clear the line buffer to begin accumulating the next line
      line_write_pos = 0;
    }
  }

  // Close /proc/self/maps
  close(maps_fd);
}
#endif  // __linux__

/**
 * Main signal handler used to dump crash reports to disk for later processing by the
 * SDK. Invoked when the process receives any of the fatal signals registered via
 * sigaction() when the SDK initializes crash tracking.
 *
 * Runs on an alternate stack to avoid stack exhaustion issues. Must be entirely
 * async-signal-safe: no malloc, no mutexes, no stdio, no C++ exceptions, no cstdlib
 * functions except for a signal-safe subset, etc.
 */
static void crash_signal_handler(int sig, siginfo_t* info, void* ucontext_raw) {
  // We should have a file descriptor for our crash report file, opened when crash
  // reporting was initialized. If we have no such file, exit immediately.
  int fd = s_crash_fd;
  if (fd < 0) {
    // 128 + sig == death by signal
    _exit(128 + sig);
  }

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
  write_newline(fd);

  write_str(fd, "Signal code: ");
  write_uint(fd, info->si_code);
  write_newline(fd);

  write_str(fd, "Fault address: ");
  write_hex_address(fd, reinterpret_cast<uintptr_t>(info->si_addr));
  write_newline(fd);

  write_str(fd, "PID: ");
  write_uint(fd, getpid());
  write_newline(fd);

  write_str(fd, "TID: ");
  write_uint(fd, reinterpret_cast<uintptr_t>(pthread_self()));
  write_newline(fd);

  // Timestamp (simple format, async-signal-safe)
  time_t now = time(nullptr);
  write_str(fd, "Timestamp: ");
  write_uint(fd, now);
  write_newline(fd);

  // The provided ucontext value contains CPU register states saved at time of crash:
  // different CPU architectures use different register names, and the layout of
  // ucontext_t is not standardized across Linux and macOS.
  ucontext_t* uc = reinterpret_cast<ucontext_t*>(ucontext_raw);

  // Retrieve the values of two key registers required to reconstruct the call stack:
  // - ip (instruction pointer / program counter): indicates the address of the
  //   instruction that was executing when the crash occurred
  // - fp (frame pointer / base pointer): indicates the address of the topmost stack
  //   frame, allowing us to walk through stack frames collecting return addresses that
  //   indicate the chain of calls leading to the crash
  void* ip = nullptr;
  void* fp = nullptr;
  // NOLINTBEGIN(performance-no-int-to-ptr)
#ifdef __APPLE__
#ifdef __x86_64__
  // macOS x64: rip/rbp
  ip = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rip);
  fp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__rbp);
#elif defined(__aarch64__)
  // macOS arm64: pc/fp
  ip = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__pc);
  fp = reinterpret_cast<void*>(uc->uc_mcontext->__ss.__fp);
#else
#error "Unsupported architecture for in-process crash handler on macOS"
#endif
#else
#ifdef __x86_64__
  // Linux x64: rip/rbp
  ip = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RIP]);
  fp = reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RBP]);
#elif defined(__aarch64__)
  // Linux arm64: pc/fp (ARM64's x29 register = FP)
  ip = reinterpret_cast<void*>(uc->uc_mcontext.pc);
  fp = reinterpret_cast<void*>(uc->uc_mcontext.regs[29]);
#else
#error "Unsupported architecture for in-process crash handler on Linux"
#endif
#endif
  // NOLINTEND(performance-no-int-to-ptr)

  // Write stack trace
  write_str(fd, "\nStack trace (raw addresses):\n");
  write_stack_trace(fd, ip, fp);

  // Write loaded modules
  write_str(fd, "\nLoaded Modules:\n");
  write_modules(fd);

  write_str(fd, "\n=== End of crash report ===\n");

  // TODO(WIP): Call old signal handler

  // Terminate the process, indicating death by signal, using _exit to skip atexit
  // handlers and stdio flushing. We intentionally leave the crash report file
  // descriptor open, since close() could fail or hang: the kernel will clean up on
  // process exit.
  _exit(128 + sig);
}

/**
 * POSIX implementation of an in-process crash handler, supporting macOS and Linux.
 *
 * With this implementation:
 *
 * - Initialize() preemptively opens a crash report file, allocates a signal-safe stack,
 *   then registers signal handlers via sigaction(), such that crash_signal_handler()
 *   will be called for the first fatal signal received by the process.
 *
 *   - crash_signal_handler() examines register state at time of crash, reconstructs the
 *     callstack, resolves the paths and address ranges of loaded binary images, and
 *     writes that information (along with summary details) to the crash report file
 *     before terminating the process.
 *
 * - Shutdown() handles clean SDK shutdown by unregistering signal handlers, then
 *   closing and deleting the unused crash report file. The signal-safe stack is NOT
 *   freed.
 */
class InProcessCrashHandler final : public ICrashHandler {
 public:
  explicit InProcessCrashHandler(impl::DiagnosticLogger& logger) : _logger(logger) {}

  /**
   * Initializes in-process crash handling by pre-opening a timestamped, PID-tagged
   * crash report file, allocating an alternate signal stack, and registering handlers
   * for all fatal signals. If all steps succeed, returns true; if any step fails,
   * restores original state and returns false.
   */
  bool Initialize() override {
    // Set up the crash handler in stages, cleaning up on failure at each step

    // Create directory to contain crashes, aborting on failure
    // TODO(WIP): Store crashes relative to SDK storage root
    if (mkdir(".crashes", 0755) < 0 && errno != EEXIST) {
      _logger.Error("Failed to create .crashes directory");
      return false;
    }

    // Format a filename for this process's crash report file, with timestamp and PID:
    // note that the mere presence of this file does not indicate that a crash occurred,
    // and the timestamp is SDK start time (to ensure uniqueness), not crash time
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

    // Preemptively open the crash report file and keep it open indefinitely
    s_crash_fd = open(s_crash_filename, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (s_crash_fd < 0) {
      _logger.Error("Failed to open crash file for writing");
      return false;
    }

    // Allocate an alternate signal stack of at least 128 KiB
    const size_t stack_size =
        std::max(static_cast<size_t>(SIGSTKSZ), static_cast<size_t>(0x20000));
    s_sigalt_stack = malloc(stack_size);
    if (!s_sigalt_stack) {
      close(s_crash_fd);
      s_crash_fd = -1;
      _logger.Error("Failed to allocate signal stack");
      return false;
    }

    // Install the alternate signal stack with sigaltstack
    // TODO(WIP): sigaltstack() is per-thread: if we wanted to reliably capture crashes
    // on all threads, even in cases that require an alternate stack, we'd need to
    // expose an API function allowing to application to register a newly-spawned
    // thread, and that function would need to allocate and register a separate
    // thread_local stack per thread (playing nice with existing alt stacks)
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

    // Prepare signal handler flags:
    // - SA_SIGINFO: Handler confirms to 3-arg sa_sigaction signature
    // - SA_ONSTACK: Run handler on alternate stack
    // - SA_RESTART: Restart interrupted syscalls (if we somehow don't terminate)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
    sigemptyset(&sa.sa_mask);

    // Register our signal handler for all fatal signals, reverting and cleaning up any
    // to avoid partial setup if any sigaction() call fails:
    /// - SIGSEGV: Segmentation fault (invalid memory access)
    /// - SIGBUS: Bus error (misaligned access, hardware fault)
    /// - SIGILL: Illegal instruction (invalid opcode)
    /// - SIGFPE: Floating-point exception (divide by zero)
    /// - SIGABRT: Abort (assert failure, explicit abort())
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
      free(s_sigalt_stack);
      s_sigalt_stack = nullptr;
      close(s_crash_fd);
      s_crash_fd = -1;
      _logger.Error("Failed to install signal handlers");
      return false;
    }

    _logger.Debug("In-process crash handler initialized successfully");
    return true;
  }

  /**
   * Uninitializes the handler in the event of a clean shutdown in a process where no
   * crashes occurred.
   */
  void Shutdown() override {
    // Restore old signal handlers
    // TODO: What if initialization failed and we didn't register anything?
    sigaction(SIGABRT, &s_old_sigabrt, nullptr);
    sigaction(SIGFPE, &s_old_sigfpe, nullptr);
    sigaction(SIGILL, &s_old_sigill, nullptr);
    sigaction(SIGBUS, &s_old_sigbus, nullptr);
    sigaction(SIGSEGV, &s_old_sigsegv, nullptr);

    // If we allocated and registered an alternate stack for this thread, disable it and
    // free it
    if (s_sigalt_stack) {
      // Disable the alternate signal stack for this thread
      // TODO(WIP): sigaltstack() is per-thread
      // TODO(WIP): Save and restore previous alt stack for this thread if replaced
      stack_t disable = {};
      disable.ss_flags = SS_DISABLE;
      sigaltstack(&disable, nullptr);

      free(s_sigalt_stack);
      s_sigalt_stack = nullptr;
    }

    // If we opened a crash file, close that file and delete it
    if (s_crash_fd >= 0) {
      // Sanity-check the file descriptor to make sure we didn't write anything to it
      const off_t num_bytes_written = lseek(s_crash_fd, 0L, SEEK_CUR);

      // Close the file
      close(s_crash_fd);
      s_crash_fd = -1;

      // If we're certain the file is empty, attempt to delete it, ignoring failure
      // since empty files will be handled cleanly by subsequent SDK instances
      if (num_bytes_written == 0) {
        int result = unlink(s_crash_filename);
        (void)result;
      }
    }
  }

 private:
  impl::DiagnosticLogger& _logger;
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

// NOLINTEND(readability-use-std-min-max)
// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
// NOLINTEND(cppcoreguidelines-pro-type-member-init)
// NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTEND(cppcoreguidelines-owning-memory)
// NOLINTEND(cppcoreguidelines-no-malloc)
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace datadog::platform
