// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

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

// TODO(RUM-14016): Add DeviceInfo struct for device metadata collection

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
   * Returns operating system information collected at initialization.
   *
   * The returned reference remains valid for the lifetime of the ISystemInfo instance.
   */
  virtual const OsInfo& GetOsInfo() const = 0;

  // TODO(RUM-14016): Add GetDeviceInfo() method to return device metadata
};

namespace SystemInfo {
/**
 * Creates an ISystemInfo instance, collecting system information from the platform.
 *
 * Platform-specific implementations use native APIs to gather OS metadata.
 * If collection fails, diagnostic warnings are emitted and default values are used.
 *
 * @param logger Diagnostic logger for emitting warnings on collection failures
 * @return Platform-specific ISystemInfo implementation
 */
std::unique_ptr<ISystemInfo> Init(impl::DiagnosticLogger& logger);
};  // namespace SystemInfo

}  // namespace datadog::platform
