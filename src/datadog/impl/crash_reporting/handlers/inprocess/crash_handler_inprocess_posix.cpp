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
#include <sys/time.h>
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

#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/storage/filesystem.hpp"
#include "datadog/impl/core/storage/path.hpp"
#include "datadog/impl/core/storage/util.hpp"
#include "datadog/impl/core/util/assert.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/crash_reporting/crash_handler.hpp"
#include "datadog/impl/crash_reporting/data/crash_context_write.hpp"
#include "datadog/impl/crash_reporting/data/crash_report_write.hpp"
#include "datadog/impl/crash_reporting/handlers/inprocess/buildid_cache.hpp"

namespace datadog::impl {

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

// Pre-opened file descriptor for crash report file to be written, along with paths to
// the crash report file and its accompanying .ctx file
static volatile sig_atomic_t s_crash_fd = -1;
static StoragePath s_crash_file_path_buf;              // <crash>
static StoragePath s_crash_context_file_path_buf;      // <crash>.ctx
static StoragePath s_crash_context_tmp_file_path_buf;  // <crash>.ctx.tmp

// StoragePath values actually hold the path strings; we just need these wrappers (which
// just hold a pointer to the StoragePath buffer) for compatibility with our
// cross-platform filesystem API
static PlatformPath s_crash_file_path;
static PlatformPath s_crash_context_file_path;
static PlatformPath s_crash_context_tmp_file_path;

// Atomically set to 1 once our signal handler has been called, and never reset. This
// one-shot reentrancy guard prevents recursive crashes (e.g., if the handler itself
// triggers a signal) from causing infinite recursion or corrupting the crash report.
static volatile sig_atomic_t s_in_handler = 0;

// Original signal handlers to restore on shutdown
static struct sigaction s_old_sigsegv;
static struct sigaction s_old_sigbus;
static struct sigaction s_old_sigill;
static struct sigaction s_old_sigfpe;
static struct sigaction s_old_sigabrt;

// Alternate stack for signal handler, to prevent stack overflow if crash was caused by
// stack exhaustion
static void* s_sigalt_stack = nullptr;

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
 * Writes the instruction pointer value indicating where the crash occurred, followed by
 * the return addresses stored in each subsequent stack frame.
 *
 * Traverses the stack, starting from the frame_pointer address that's currently loaded
 * in the RBP/FP register, writing each stack frame using WriteCrashReportStackFrame().
 */
static void write_stack_trace(int fd, void* instruction_pointer, void* frame_pointer) {
  // The topmost (most-recently-pushed) frame in our callstack represents the function
  // in which the crash occurred: the value of the instruction pointer / program counter
  // at the time of the crash indicates exactly where the crash occurred
  WriteCrashReportStackFrame(fd, reinterpret_cast<uint64_t>(instruction_pointer));

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
    WriteCrashReportStackFrame(fd, reinterpret_cast<uint64_t>(ret_addr));

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

/**
 * Returns pointer to the saved sigaction struct for the given signal number, or nullptr
 * if the signal is not one that we handle.
 */
static struct sigaction* get_old_sigaction(int sig) {
  switch (sig) {
    case SIGSEGV:
      return &s_old_sigsegv;
    case SIGBUS:
      return &s_old_sigbus;
    case SIGILL:
      return &s_old_sigill;
    case SIGFPE:
      return &s_old_sigfpe;
    case SIGABRT:
      return &s_old_sigabrt;
    default:
      return nullptr;
  }
}

// === Enumeration of loaded modules ===
// - Uses dyld APIs on macOS; reads from /proc/self/maps on Linux

/**
 * Resolves a list of all modules that are loaded in the current process, and writes
 * them to the given file descriptor in binary format, using WriteCrashReportModule.
 */
static void write_modules(int fd);

#ifdef __APPLE__

/**
 * Extracts the UUID from a Mach-O binary header by parsing load commands for
 * LC_UUID. Returns pointer to `out_buffer` containing formatted UUID on success, or
 * nullptr if LC_UUID not found. Async-signal-safe.
 *
 * The UUID is formatted as lowercase hex with dashes:
 * xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 chars + null terminator)
 */
static const char* extract_macho_uuid(
    const struct mach_header* header, char* out_buffer, size_t buffer_size
) {
  if (buffer_size < 37) {
    return nullptr;
  }

  // Helper to convert nibble to hex character
  auto hex_char = [](uint8_t nibble) -> char {
    return static_cast<char>(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
  };

  // Locate load commands after mach header
  const uint8_t* cmd_ptr = nullptr;
  uint32_t ncmds = 0;
  if (header->magic == MH_MAGIC_64 || header->magic == MH_CIGAM_64) {
    const auto* header_64 = reinterpret_cast<const struct mach_header_64*>(header);
    cmd_ptr = reinterpret_cast<const uint8_t*>(header_64 + 1);
    ncmds = header_64->ncmds;
  } else {
    cmd_ptr = reinterpret_cast<const uint8_t*>(header + 1);
    ncmds = header->ncmds;
  }

  // Iterate through load commands to find LC_UUID
  for (uint32_t i = 0; i < ncmds; i++) {
    const auto* cmd = reinterpret_cast<const struct load_command*>(cmd_ptr);

    if (cmd->cmd == LC_UUID) {
      // LC_UUID command contains 16-byte UUID immediately after load_command header
      const uint8_t* uuid_bytes = cmd_ptr + sizeof(struct load_command);

      // Format UUID as lowercase hex with dashes at positions 8, 13, 18, 23
      char* out = out_buffer;
      for (int byte_idx = 0; byte_idx < 16; byte_idx++) {
        uint8_t byte = uuid_bytes[byte_idx];
        *out++ = hex_char(byte >> 4);
        *out++ = hex_char(byte & 0x0F);

        // Insert dashes after bytes 3, 5, 7, 9 (positions 8, 13, 18, 23 in output)
        if (byte_idx == 3 || byte_idx == 5 || byte_idx == 7 || byte_idx == 9) {
          *out++ = '-';
        }
      }
      *out = '\0';
      return out_buffer;
    }

    cmd_ptr += cmd->cmdsize;
  }

  return nullptr;
}

#endif

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

    // Extract build ID (UUID) from Mach-O header
    char build_id_buffer[64];
    const char* build_id =
        extract_macho_uuid(header, build_id_buffer, sizeof(build_id_buffer));

    // Write the relevant details of this module
    WriteCrashReportModule(
        fd,
        static_cast<uint64_t>(actual_start),
        static_cast<uint64_t>(actual_end),
        image_name,
        build_id
    );
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
    // Failed to open /proc/self/maps; silently return (best-effort crash handling)
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

      // Retrieve build ID from cache
      const char* build_id = FindCachedBuildId(start_addr);

      // We've parsed the address range and pathname of a loaded module that should be
      // included in our crash report: write that information to the output file
      WriteCrashReportModule(
          fd,
          static_cast<uint64_t>(start_addr),
          static_cast<uint64_t>(end_addr),
          pathname,
          build_id
      );

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
  // One-shot reentrancy guard: if this function is called more than once (e.g., because
  // the handler itself triggers a signal during crash report generation), we'll take no
  // action and immediately terminate to avoid infinite recursion or report corruption
  if (__atomic_exchange_n(&s_in_handler, 1, __ATOMIC_SEQ_CST) != 0) {
    // Handler already running: terminate immediately with standard signal death code
    _exit(128 + sig);
  }

  // We should have a file descriptor for our crash report file, opened when crash
  // reporting was initialized. If we have no such file, do not attempt to handle the
  // crash: instead, chain to any previously-registered handler, and terminate the
  // process if needed.
  int fd = s_crash_fd;
  if (fd < 0) {
    struct sigaction* old_action = get_old_sigaction(sig);
    if (old_action) {
      sigaction(sig, old_action, nullptr);
      raise(sig);
    }
    // No old_action, or old_action did not terminate. Exit with 128 + sig to indicate
    // death by signal
    _exit(128 + sig);
  }

  // We have a crash report file open and we should handle this crash; proceed with
  // writing to that file before chaining and/or exiting
  WriteCrashReportHeader(
      fd,
      static_cast<uint64_t>(sig),                  // fault_code (signal number)
      reinterpret_cast<uint64_t>(info->si_addr),   // fault_address
      0,                                           // fault_flags (0 on POSIX)
      static_cast<uint64_t>(getpid()),             // pid
      reinterpret_cast<uint64_t>(pthread_self()),  // tid
      static_cast<uint64_t>(time(nullptr))         // timestamp
  );

  // The provided ucontext value contains CPU register states saved at time of crash:
  // different CPU architectures use different register names, and the layout of
  // ucontext_t is not standardized across Linux and macOS. Copying into a local buffer
  // ensures correct alignment on access, as ucontext_t requires 16-byte alignment but
  // ucontext_raw may not be 16-byte-aligned.
  alignas(ucontext_t) unsigned char ucontext_buf[sizeof(ucontext_t)];
  for (size_t i = 0; i < sizeof(ucontext_t); i++) {
    ucontext_buf[i] = reinterpret_cast<const unsigned char*>(ucontext_raw)[i];
  }
  ucontext_t* uc = reinterpret_cast<ucontext_t*>(ucontext_buf);

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

  // Write loaded modules
  write_modules(fd);

  // Write stack trace
  write_stack_trace(fd, ip, fp);

  // Write footer to mark end of crash report
  WriteCrashReportFooter(fd);

  // If our signal handler replaced another signal handler that was installed by another
  // library, by the application, or by the OS before us, restore that signal handler
  // and re-raise the signal, effectively chaining to that signal handler so that we
  // don't eat the error
  struct sigaction* old_action = get_old_sigaction(sig);
  if (old_action) {
    sigaction(sig, old_action, nullptr);
    raise(sig);
  }

  // If we didn't have a previous handler to chain to, _or_ if that handler didn't
  // terminate the process, we'll still have control of the program here. Terminate the
  // process, indicating death by signal, using _exit to skip atexit handlers and stdio
  // flushing. We intentionally leave the crash report file descriptor open, since
  // close() could fail or hang: the kernel will clean up on process exit.
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
  InProcessCrashHandler() = default;
  ~InProcessCrashHandler() override { Shutdown(); }
  InProcessCrashHandler(const InProcessCrashHandler&) = delete;
  InProcessCrashHandler& operator=(const InProcessCrashHandler&) = delete;
  InProcessCrashHandler(InProcessCrashHandler&&) = delete;
  InProcessCrashHandler& operator=(InProcessCrashHandler&&) = delete;

  /**
   * Initializes in-process crash handling by pre-opening a timestamped, PID-tagged
   * crash report file, allocating an alternate signal stack, and registering handlers
   * for all fatal signals. If all steps succeed, returns true; if any step fails,
   * restores original state and returns false.
   */
  bool Initialize(
      DiagnosticLogger logger,
      IFilesystem& fs,
      const StoragePath& crash_storage_dir_path,
      std::string_view helper_exe_path
  ) override {
    // The in-process handler doesn't use a helper executable
    (void)helper_exe_path;

    // Set up the crash handler in stages, cleaning up on failure at each step
    DATADOG_ASSERT(!_initialized, "InProcessCrashHandler::Initialize called twice");

    // Get the current system timestamp, in milliseconds, for use in filenames
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t timestamp_ms = (static_cast<uint64_t>(ts.tv_sec) * 1000) +
                            (static_cast<uint64_t>(ts.tv_nsec) / 1000000);

    // Prepare the filename for crash reports written by this process:
    // crash_<system-timestamp-in-ms>_<pid>
    char crash_filename[64];
    snprintf(
        crash_filename,
        sizeof(crash_filename),
        "crash_%" PRIu64 "_%d",
        timestamp_ms,
        getpid()
    );

    // Build the full path to a file with that name in the .datadog/.crashes/ directory
    s_crash_file_path_buf.MustSet(crash_storage_dir_path);
    if (!s_crash_file_path_buf.Append(crash_filename)) {
      logger.Error("Failed to initialize in-process crash handler: path too long");
      return false;
    }

    // Build the path to an accompanying crash_<system-timestamp-in-ms>_<pid>.ctx file
    s_crash_context_file_path_buf.MustSet(s_crash_file_path_buf);
    if (!s_crash_context_file_path_buf.AppendExt(".ctx")) {
      logger.Error("Failed to initialize in-process crash handler: path too long");
      return false;
    }

    // Append .tmp so we can use a write-then-rename pattern for context file updates
    s_crash_context_tmp_file_path_buf.MustSet(s_crash_context_file_path_buf);
    if (!s_crash_context_tmp_file_path_buf.AppendExt(".tmp")) {
      logger.Error("Failed to initialize in-process crash handler: path too long");
      return false;
    }

    // "Encode" paths, giving us PlatformPath values that we can pass to IFilesystem
    if (!s_crash_file_path.Encode(s_crash_file_path_buf.CStr()) ||
        !s_crash_context_file_path.Encode(s_crash_context_file_path_buf.CStr()) ||
        !s_crash_context_tmp_file_path.Encode(
            s_crash_context_tmp_file_path_buf.CStr()
        )) {
      // Path encoding is a transparent no-op on POSIX, so this can not fail
      DATADOG_ASSERT(false, "Unexpected path encoding failure in POSIX crash handler");
      logger.Error(
          "Failed to initialize in-process crash handler: path encoding failed"
      );
      return false;
    }

    // On POSIX, PlatformPath just holds a pointer to the underlying StoragePath value
    // that was last passed to Encode(): hence we store both sets of values
    // indefinitely
    static_assert(
        sizeof(PlatformPath) == sizeof(void*), "Unexpected PlatformPath size"
    );

    // Preemptively open the crash report file and keep it open indefinitely
    const bool append = false;
    const bool hold_advisory_lock = true;
    auto open_res = fs.OpenForWrite(s_crash_file_path, append, hold_advisory_lock);
    if (open_res.value != FilesystemResult::OK) {
      logger.Error(
          "Failed to initialize in-process crash handler: could not create crash file",
          {{"path", s_crash_file_path_buf.Get()},
           {"error", FilesystemResultStr(open_res.value)}}
      );
      return false;
    }
    s_crash_fd = open_res.handle;

    // On POSIX, PlatformFileHandle is an alias for int, which is functionally
    // equivalent to sig_atomic_t, so we can use native POSIX functions to write to this
    // file descriptor in the async-signal-safe crash path
    static_assert(
        std::is_same_v<std::remove_volatile_t<decltype(s_crash_fd)>, int>,
        "Unexpected file handle type"
    );

    // Allocate an alternate signal stack of at least 128 KiB
    const size_t stack_size =
        std::max(static_cast<size_t>(SIGSTKSZ), static_cast<size_t>(0x20000));
    s_sigalt_stack = malloc(stack_size);
    if (!s_sigalt_stack) {
      close(s_crash_fd);
      s_crash_fd = -1;
      logger.Error("Failed to allocate signal stack");
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
      logger.Error("Failed to install signal stack");
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
    // to avoid partial setup if any sigaction() call fails. Note that s_old_sigsegv et
    // al. will _always_ be populated with a valid value.
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
      logger.Error("Failed to install signal handlers");
      return false;
    }

    logger.Debug("In-process crash handler initialized successfully");
    _initialized = true;

    // Initialize build ID cache for crash-time lookup (Windows/Linux only)
    PopulateBuildIdCache();

    return true;
  }

  /**
   * Uninitializes the handler in the event of a clean shutdown in a process where no
   * crashes occurred.
   */
  void Shutdown() {  // NOLINT(readability-make-member-function-const)
    // If we weren't successfully initialized, there should be no cleanup needed
    if (!_initialized) {
      return;
    }

    // Restore any old signal handlers that were replaced when we registered ours
    sigaction(SIGABRT, &s_old_sigabrt, nullptr);
    sigaction(SIGFPE, &s_old_sigfpe, nullptr);
    sigaction(SIGILL, &s_old_sigill, nullptr);
    sigaction(SIGBUS, &s_old_sigbus, nullptr);
    sigaction(SIGSEGV, &s_old_sigsegv, nullptr);

    // Disable the alternate signal stack for this thread
    // TODO(WIP): sigaltstack() is per-thread
    // TODO(WIP): Save and restore previous alt stack for this thread if replaced
    stack_t disable = {};
    disable.ss_flags = SS_DISABLE;
    sigaltstack(&disable, nullptr);

    // Free the memory we allocated for the alternate stack
    DATADOG_ASSERT(
        s_sigalt_stack,
        "s_sigalt_stack should be allocated if crash handler was fully initialized"
    );
    free(s_sigalt_stack);
    s_sigalt_stack = nullptr;

    // We should have a crash report file open: clean it up since we didn't crash
    DATADOG_ASSERT(
        s_crash_fd >= 0,
        "s_crash_fd should be open if crash handler was fully initialized"
    );

    // Sanity-check the file descriptor to make sure we didn't write anything to it
    const off_t num_bytes_written = lseek(s_crash_fd, 0L, SEEK_CUR);

    // Close the file
    close(s_crash_fd);
    s_crash_fd = -1;

    // If we're certain the file is empty, attempt to delete it, ignoring failure
    // since empty files will be handled cleanly by subsequent SDK instances
    if (num_bytes_written == 0) {
      int result = unlink(s_crash_file_path.Get());
      if (result == 0) {
        // Also delete any accompanying context files, ignoring result since file
        // cleanup is best-effort
        result = unlink(s_crash_context_file_path.Get());
        (void)result;
        result = unlink(s_crash_context_tmp_file_path.Get());
        (void)result;
      }
    }
  }

  void SetCrashContext(IFilesystem& fs, const CrashContext& ctx) override {
    WriteCrashContext(fs, s_crash_context_file_path, s_crash_context_tmp_file_path, ctx);
  }

 private:
  bool _initialized{false};
};

std::unique_ptr<ICrashHandler> CrashHandler::Create() {
  // Construct a new POSIX in-process crash handler
  return std::make_unique<InProcessCrashHandler>();
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

}  // namespace datadog::impl
