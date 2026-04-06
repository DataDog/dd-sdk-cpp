// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace datadog::impl {
class DiagnosticLogger;
}  // namespace datadog::impl

namespace datadog::platform {

/**
 * Operating system information collected at SDK startup.
 *
 * All fields are pre-formatted as strings for direct inclusion in events.
 */
struct OsInfo {
  /** Operating system name (e.g., "Windows", "macOS", "Ubuntu") */
  std::string name;

  /** Operating system version (e.g., "10.0.22631", "14.2.1", "22.04") */
  std::string version;

  /** Operating system build identifier (e.g., "22631.6491", "23C71") */
  std::string build;

  /** Major version component, parsed from version string (e.g., "10", "14", "22") */
  std::string version_major;
};

/**
 * Device information collected at SDK startup.
 *
 * All fields are pre-formatted as strings for direct inclusion in events.
 * Empty strings indicate unavailable values and will be omitted from JSON.
 */
struct DeviceInfo {
  /** Device type (e.g., "desktop") */
  std::string type;

  /** Device name (e.g., "MacBookPro", "Latitude 7420", "ThinkPad X1 Carbon") */
  std::string name;

  /** Device model (e.g., "MacBookPro16,1", "Z790 UC AC", "1.0") */
  std::string model;

  /** Device brand/manufacturer (e.g., "Apple", "Dell Inc.", "LENOVO") */
  std::string brand;

  /** CPU architecture (e.g., "x86_64", "arm64", "x86") */
  std::string architecture;

  /** User locale (e.g., "en-US", "fr-FR") */
  std::string locale;

  /** IANA timezone (e.g., "America/New_York", "Europe/Berlin") */
  std::string time_zone;
};

/**
 * Interface for accessing system information.
 *
 * System information is collected once during SDK initialization and cached
 * for the lifetime of the SDK instance.
 */
class ISystemInfo {
 protected:
  ISystemInfo() = default;

 public:
  virtual ~ISystemInfo() = default;

  // Non-copyable, non-movable: implementations may hold platform-specific resources
  ISystemInfo(const ISystemInfo&) = delete;
  ISystemInfo& operator=(const ISystemInfo&) = delete;
  ISystemInfo(ISystemInfo&&) = delete;
  ISystemInfo& operator=(ISystemInfo&&) = delete;

  /**
   * Returns the PID of the current process as an `int64_t`, cast from the
   * native type (`pid_t` on POSIX, `DWORD` on Windows).
   *
   * The caller is responsible for validating the returned value (e.g.,
   * verifying it is positive before use).
   */
  virtual int64_t GetPid() const = 0;

  /**
   * Returns operating system information collected at initialization.
   *
   * The returned reference remains valid for the lifetime of the ISystemInfo instance.
   */
  virtual const OsInfo& GetOsInfo() const = 0;

  /**
   * Returns device information collected at initialization.
   *
   * The returned reference remains valid for the lifetime of the ISystemInfo instance.
   */
  virtual const DeviceInfo& GetDeviceInfo() const = 0;
};

namespace SystemInfo {
/**
 * Constructs an ISystemInfo instance, collecting system information from the machine on
 * which the application is running. The resulting ISystemInfo value, if valid, will be
 * fully initialized with all available OS and device information.
 *
 * If the SDK is unable to resolve values, it will emit diagnostic messages with
 * DiagnosticLevel::Debug, and default/fallback values will be used.
 */
std::unique_ptr<ISystemInfo> Init(impl::DiagnosticLogger& logger);
};  // namespace SystemInfo

}  // namespace datadog::platform
