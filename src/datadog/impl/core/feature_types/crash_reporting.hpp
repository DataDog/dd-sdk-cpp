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

#include "datadog/attribute.hpp"
#include "datadog/core.hpp"

#include "datadog/impl/core/feature_types/rum.hpp"

namespace datadog::impl {

/**
 * Essential SDK state persisted to disk alongside each crash report.
 *
 * `CrashReporting` holds a single `CrashContext` value. Whenever `CrashContext`
 * receives a message indicating that SDK state has changed in a way that affects these
 * values, it updates that value, then persists it via the ICrashHandler to ensure that
 * any crash that's subsequently handled will be associated with the latest snapshot of
 * SDK state.
 *
 * When on-disk crash reports are processed and relayed to other features to be
 * processed on next launch, any accompanying `CrashContext` is deserialized and sent
 * along with the crash. This allows `Rum`, for example, to handle crash reports in a
 * way that's fully informed by the state of the SDK at the time of the crash.
 */
struct CrashContext {
  // Application configuration details
  std::string service;
  std::string env;
  std::string application_version;
  std::string variant;

  // Internal SDK configuration details
  std::string source;
  std::string sdk_version;

  // Current SDK instance state
  TrackingConsent tracking_consent{TrackingConsent::Pending};

  // OS info populated in the process that crashed
  std::string os_name;
  std::string os_version;
  std::string os_build;
  std::string os_version_major;

  // Device info populated in the process that crashed
  std::string device_type;
  std::string device_name;
  std::string device_model;
  std::string device_brand;
  std::string device_architecture;
  std::string device_locale;
  std::string device_time_zone;

  // User details conveyed to the SDK via SetUserInfo() et al. in the process that
  // crashed; empty if no user info was set
  std::string user_id;
  std::string user_name;
  std::string user_email;
  UUID user_anonymous_id;  // UUID::Zero if anonymous_id not enabled or not set
  Attribute user_extra;

  // Account details from SetAccountInfo()
  std::string account_id;
  std::string account_name;
  Attribute account_extra;

  // Configured RUM session sample rate (0.0–100.0); 0.0 if RUM was not configured
  float rum_session_sample_rate{0.0f};

  // State of the latest RUM session prior to the crash. If no RUM session was ever
  // created, session_id will be UUID::Zero.
  RumSessionState rum_session_state{};

  // The last RUM View event that was generated to describe the view that was active at
  // the time of the crash; or empty if no RUM view was active
  std::string last_view_event_json;

  // Global RUM attributes at time of crash
  Attribute global_rum_attributes;
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
  uint64_t timestamp_ms;   // Unix timestamp in ms, read from system clock at crash time

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
