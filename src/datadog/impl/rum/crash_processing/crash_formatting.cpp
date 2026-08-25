// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/crash_processing/crash_formatting.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <string>

#include "datadog/impl/types/assert.hpp"
#include "datadog/impl/types/crash_reporting.hpp"

namespace datadog::impl {

#ifdef _WIN32
/**
 * Formats an error message given a Win32 structured exception code, e.g.:
 *
 * - `Application crash: EXCEPTION_ACCESS_VIOLATION (0xC0000005)`
 * - `Application crash: 0xCDCDCDCD` <unrecognized SEH code>
 *
 * `fault_code` is assumed to be a DWORD-length exception code and will be truncated to
 * 32 bits.
 */
static std::string format_error_message_win32(uint64_t fault_code) {
  // Truncate to lower 32 bits to avoid sign-extension on comparison; structured
  // exception codes are always 32-bit
  const uint32_t code = static_cast<uint32_t>(fault_code);

  // Use a local buffer to format the code as an 8-digit, zero-padded, '0x'-prefixed
  // string, as is the Win32 convention
  char hex_code_buf[11];  // "0x" + 8 hex digits + terminator
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  std::snprintf(&hex_code_buf[0], sizeof(hex_code_buf), "0x%08X", code);
  std::string_view hex_code(&hex_code_buf[0], sizeof(hex_code_buf) - 1);

  // Check the code against a list of common codes to resolve a human-readable name
  struct KnownExceptionCode {
    uint32_t code;
    const char* name;
  };
  static constexpr KnownExceptionCode KNOWN_EXCEPTION_CODES[] = {
      {0x40000015, "FATAL_APP_EXIT"},
      {0x80000001, "EXCEPTION_GUARD_PAGE"},
      {0x80000002, "EXCEPTION_DATATYPE_MISALIGNMENT"},
      {0x80000003, "EXCEPTION_BREAKPOINT"},
      {0x80000004, "EXCEPTION_SINGLE_STEP"},
      {0xC0000005, "EXCEPTION_ACCESS_VIOLATION"},
      {0xC0000006, "EXCEPTION_IN_PAGE_ERROR"},
      {0xC000001D, "EXCEPTION_ILLEGAL_INSTRUCTION"},
      {0xC0000025, "EXCEPTION_NONCONTINUABLE_EXCEPTION"},
      {0xC0000026, "EXCEPTION_INVALID_DISPOSITION"},
      {0xC000008C, "EXCEPTION_ARRAY_BOUNDS_EXCEEDED"},
      {0xC000008D, "EXCEPTION_FLT_DENORMAL_OPERAND"},
      {0xC000008E, "EXCEPTION_FLT_DIVIDE_BY_ZERO"},
      {0xC000008F, "EXCEPTION_FLT_INEXACT_RESULT"},
      {0xC0000090, "EXCEPTION_FLT_INVALID_OPERATION"},
      {0xC0000091, "EXCEPTION_FLT_OVERFLOW"},
      {0xC0000092, "EXCEPTION_FLT_STACK_CHECK"},
      {0xC0000093, "EXCEPTION_FLT_UNDERFLOW"},
      {0xC0000094, "EXCEPTION_INT_DIVIDE_BY_ZERO"},
      {0xC0000095, "EXCEPTION_INT_OVERFLOW"},
      {0xC0000096, "EXCEPTION_PRIV_INSTRUCTION"},
      {0xC00000FD, "EXCEPTION_STACK_OVERFLOW"},
      {0xC0000409, "EXCEPTION_HEAP_CORRUPTION"},
      {0xC0000602, "STATUS_ASSERTION_FAILURE"},
  };
  std::string_view name;
  for (const auto& known_code : KNOWN_EXCEPTION_CODES) {
    if (known_code.code == code) {
      name = known_code.name;
      break;
    }
  }

  // If we found a matching name, format a message that includes that name along with
  // the code
  static constexpr std::string_view crash_prefix = "Application crash: ";
  if (!name.empty()) {
    std::string result;
    result.reserve(crash_prefix.size() + name.size() + 2 + hex_code.size() + 1);
    result += crash_prefix;
    result += name;
    result += " (";
    result += hex_code;
    result += ')';
    return result;
  }

  // Unknown exception code: format a message that specifies the raw code, in hex
  std::string result;
  result.reserve(crash_prefix.size() + hex_code.size());
  result += crash_prefix;
  result += hex_code;
  return result;
}
#else
/**
 * Formats an error message given a POSIX signal that resulted in a crash, e.g.:
 *
 * - `Application crash: SIGSEGV (Segmentation fault)`
 * - `Application crash: signal 3452816845` <unrecognized signal>
 */
static std::string format_error_message_posix(uint64_t fault_code) {
  // Check the code against a list of common fatal signals to resolve a pre-formatted
  // message
  struct KnownFatalSignal {
    int sig;
    const char* message;
  };
  static constexpr KnownFatalSignal KNOWN_FATAL_SIGNALS[] = {
      {SIGABRT, "Application crash: SIGABRT (Abort trap)"},
      {SIGBUS, "Application crash: SIGBUS (Bus error)"},
      {SIGFPE, "Application crash: SIGFPE (Floating-point exception)"},
      {SIGILL, "Application crash: SIGILL (Illegal instruction)"},
      {SIGSEGV, "Application crash: SIGSEGV (Segmentation fault)"},
      {SIGTRAP, "Application crash: SIGTRAP (Trace trap)"},
  };
  for (const auto& known_signal : KNOWN_FATAL_SIGNALS) {
    if (static_cast<uint64_t>(known_signal.sig) == fault_code) {
      return std::string(known_signal.message);
    }
  }

  // Unknown signal: format a message that specifies the raw signal number, in decimal
  return "Application crash: signal " + std::to_string(fault_code);
}
#endif

/**
 * Formats the stack trace for the given crash report using the format expected by
 * `parseNativeStack` in dd-source/domains/error-tracking:
 *
 * - `<idx>  <module>   0x<instr_addr_16hex> 0x<load_addr_hex> + <offset_dec>`
 *
 * This format is based on Apple's crash-log frame format, which was originally used for
 * iOS symbolication, then later generalized as the Datadog backend's "native" stack
 * format when Mach-O, ELF, and PE symbolication support were added. See
 * `crash_formatting.hpp` for full details of the format.
 *
 * A RUM Error representing a crash contains the main thread's stack trace in
 * `error.stack`, encoded as a string. The exact format that this stack trace must take
 * is dictated by the `error.source_type` assigned to the event. The backend expects
 * this stack trace format when it receives a request tagged `macho`, `elf`, or `pe`.
 *
 * For our purposes, on all three supported desktop platforms, a RUM Error event's
 * `error.source_type` value identifies the platform on which the error occurred:
 * `macos`, `linux`, or `windows`. When the frontend needs to fetch an enriched
 * callstack from the backend for a RUM Error, it translates these values to the
 * corresponding `error.source_type` values used in `source-code-query`, which reflect
 * the binary format: `macho`, `elf`, or `pe`.
 */
static std::string format_stack_error_tracking(const CrashReport& crash) {
  // If the stack has no frames, return an empty string
  if (crash.stack.empty()) {
    return "";
  }

  // Cap the maximum number of stack frames: the crash handler bails out after writing
  // 128 frames, and we'll refuse to load a crash report with more than 512 frames, so
  // this places a reasonable upper bound on the size of the string
  const size_t max_frames = 512;
  const size_t num_frames = std::min(crash.stack.size(), max_frames);

  // Preemptively determine the longest module name length that will appear in the stack
  size_t max_module_name_len = 3;  // Length of unknown-module sentinel "???"
  for (size_t frame_index = 0; frame_index < num_frames; frame_index++) {
    const auto& frame = crash.stack[frame_index];
    if (frame.module_index >= 0 &&
        static_cast<size_t>(frame.module_index) < crash.modules.size()) {
      const size_t module_name_len = crash.modules[frame.module_index].name.size();
      max_module_name_len = std::max(max_module_name_len, module_name_len);
    }
  }

  // Preallocate the result stack trace string: use our max module name length to
  // compute the worst-case length of any individual frame, then multiply by the number
  // of frames we'll be writing
  std::string result;
  const size_t max_frame_len = 3                      // "12 "
                               + 1                    // ' '
                               + max_module_name_len  // "somelibrary.lib"
                               + 1                    // '\t'
                               + 18                   // "0x0123456789abcdef"
                               + 1                    // ' '
                               + 18                   // "0x100eeffbb"
                               + 3                    // " + "
                               + 20                   // "123456"
                               + 1;                   // '\n'
  result.reserve(max_frame_len * num_frames);

  // Define a helper function to format the values in each line, building a full line
  // and appending it to the result value
  auto append_frame = [&](size_t index,
                          std::string_view module_name,
                          uint64_t raw_address,
                          uint64_t load_address,
                          uint64_t offset) {
    // Frame index is '%d', right-padded with spaces to always hit three digits,
    // followed by a space
    DATADOG_ASSERT(index <= 999, "frame_index not capped by max_frames");
    std::array<char, 3> index_buf{};
    index_buf.fill(' ');
    std::to_chars(index_buf.data(), index_buf.data() + index_buf.size(), index);
    result.append(index_buf.data(), index_buf.size());
    result.push_back(' ');

    // Next comes the module name, conventionally separated from the address values by
    // a tab
    DATADOG_ASSERT(
        module_name.size() <= max_module_name_len,
        "module name exceeds max_module_name_len"
    );
    result += module_name;
    result.push_back('\t');

    // Finally, we append an "address section" and add a newline to terminate the frame.
    // The address section consists of raw instruction address, module load address, and
    // offset, following specific formatting rules:
    //
    // - <raw_address_hex> <load_address_hex> + <offset_dec>
    //
    // Where both hex addresses are '0x'-prefixed and lowercase, and while raw_address
    // is zero-padded to 16 digits, load_address is not.
    const size_t max_addr_section_len = 18    // "0x0123456789abcdef"
                                        + 1   // ' '
                                        + 18  // "0x100eeffbb"
                                        + 3   // " + "
                                        + 20  // "123456"
                                        + 1   // '\n'
                                        + 1;  // '\0' (for snprintf)
    std::array<char, max_addr_section_len> addr_section_buf{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::snprintf(
        addr_section_buf.data(),
        addr_section_buf.size(),
        "0x%016" PRIx64 " 0x%" PRIx64 " + %" PRIu64 "\n",
        raw_address,
        load_address,
        offset
    );
    std::string_view addr_section{addr_section_buf.data()};  // Find null terminator
    result += addr_section;
  };

  // Iterate over all frames that should appear in the stack, resolving the full set of
  // frame and module information and appending an appropriately-formatted line to our
  // result value
  for (size_t frame_index = 0; frame_index < num_frames; frame_index++) {
    const auto& frame = crash.stack[frame_index];
    if (frame.module_index >= 0 &&
        static_cast<size_t>(frame.module_index) < crash.modules.size()) {
      // "12  somelibrary.lib\t0x0123456789abcdef 0x100eeffbb + 123456\n"
      const auto& module = crash.modules[frame.module_index];
      append_frame(
          frame_index, module.name, frame.address, module.start_address, frame.offset
      );
    } else {
      // "12  ???\t0x0123456789abcdef 0x0 + 0\n"
      append_frame(frame_index, "???", frame.address, 0, 0);
    }
  }

  // Return our resulting string, which now contains a line for each stack frame, with a
  // trailing newline
  return result;
}

std::string FormatCrashReportErrorMessage(const CrashReport& crash) {
#ifdef _WIN32
  return format_error_message_win32(crash.fault_code);
#else
  return format_error_message_posix(crash.fault_code);
#endif
}

std::string FormatCrashReportStack(const CrashReport& crash) {
  return format_stack_error_tracking(crash);
}

}  // namespace datadog::impl
