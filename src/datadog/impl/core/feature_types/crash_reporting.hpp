// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <optional>
#include <string>
#include <vector>

#include "datadog/core.hpp"

#include "datadog/impl/core/feature_types/rum.hpp"

namespace datadog::impl {

/**
 * Rich context snapshot persisted to disk alongside each crash report, capturing
 * the SDK state at the moment of the crash so it can be replayed when the crash is
 * processed on the next app launch.
 *
 * All optional values are represented as empty strings (for string fields) or
 * UUID::Zero / all-false booleans (for `rum_session_state`) rather than with
 * `std::optional` wrappers, so the type can be serialized with a flat, fixed-field
 * binary layout.
 */
struct CrashContext {
  // Core identity
  std::string service;
  std::string env;
  std::string application_version;
  std::string source;
  std::string sdk_version;
  TrackingConsent tracking_consent{TrackingConsent::Pending};

  // OS info (from platform::OsInfo)
  std::string os_name;
  std::string os_version;
  std::string os_build;
  std::string os_version_major;

  // Device info (from platform::DeviceInfo)
  std::string device_type;
  std::string device_name;
  std::string device_model;
  std::string device_brand;
  std::string device_architecture;
  std::string device_locale;
  std::string device_time_zone;

  // User info — empty strings when no user has been set via Core::SetUserInfo()
  std::string user_id;
  std::string user_name;
  std::string user_email;
  std::string user_extra_json;  // Attribute serialized as JSON; empty if not set

  // TODO(RUM-15997): Add account_id / account_name once Core::SetAccountInfo()
  // is implemented.

  // RUM session state — rum_session_state.session_id is UUID::Zero when no session
  // has been created
  RumSessionState rum_session_state{};

  // Last active view event serialized as JSON; empty when no view is active
  std::string last_view_event_json;

  // Global RUM attributes serialized as a JSON object; empty when none are set
  std::string global_attributes_json;
};

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

  // RUM and SDK state at the time of the crash; absent if no context file was found
  std::optional<CrashContext> context;

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
