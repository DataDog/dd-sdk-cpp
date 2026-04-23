// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <string>
#include <vector>

#include "datadog/uuid.hpp"

namespace datadog::impl {

/**
 * Record of a single crash in this application that the SDK has handled.
 *
 * The CrashReporting feature defines its own internal data types that closely mirror
 * how it represents the data related to a crash. Once CrashReporting processes that
 * data, it emits a value of this type in order to surface the details of the crash to
 * the wider SDK.
 */
struct CrashReport {
  uint64_t fault_code;     // Signal number or exception code
  uint64_t fault_address;  // Address that triggered signal or exception
  uint64_t fault_flags;    // Exception flags on Windows; 0 otherwise
  uint64_t pid;            // PID of crashing process
  uint64_t tid;            // Thread ID of crashing thread
  uint64_t timestamp;      // Unix timestamp read directly from system clock

  UUID rum_application_id;  // UUID::Zero if crash had no RUM context
  UUID rum_session_id;      // UUID::Zero if no RUM context or no session was active
  UUID rum_view_id;         // UUID::Zero if no RUM context or no view was active
  UUID rum_action_id;       // UUID::Zero if no RUM context or no action was active

  /**
   * Details of a loaded binary module that appears in the stack trace, ultimately used
   * to populate the `binary_images` array in a RUM Error event.
   *
   * Note that the exact values used for `arch` vary based on platform. The exact
   * semantics of the `arch` value are determined by whichever backend deobfuscation
   * implementation handles symbolication for the crash. These details are opaque to the
   * SDK.
   *
   * On macOS, for example, `arch` may be "arm64" or "x86_64" to indicate which slice of
   * a universal binary was loaded in the address space of the crashing process. On
   * other platforms, the field may use a different set of values or may be unused.
   */
  struct Module {
    std::string name;        // Filename of the executable or library
    std::string build_id;    // Unique ID of this module's binary, for deobfuscation
    std::string arch;        // Deobfuscation-backend-specific identifier for CPU arch
    bool is_system;          // Whether the file was loaded from a system-library path
    uint64_t start_address;  // Base address at which this module was loaded
    uint64_t end_address;    // End of module's address range (exclusive)
  };
  std::vector<Module> modules;

  /**
   * Details of a single entry in the stack trace for a crash, ultimately used to format
   * the `stack` string for a RUM Error event.
   */
  struct Frame {
    uint64_t address;      // Raw instruction address for this frame
    int64_t module_index;  // Index into modules array, or -1 if no module resolved
    uint64_t offset;       // Offset into that module corresponding to raw address
  };
  std::vector<Frame> stack;
};

}  // namespace datadog::impl
