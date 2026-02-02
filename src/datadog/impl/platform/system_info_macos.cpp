// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <sys/sysctl.h>

#include <string>

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/system_info.hpp"

namespace datadog::platform {

namespace {

/**
 * Retrieves a string value from sysctl by name.
 *
 * @param name sysctl variable name (e.g., "kern.osproductversion")
 * @param logger Diagnostic logger for warnings
 * @return Retrieved string value, or empty string on failure
 */
std::string GetSysctlString(const char* name, impl::DiagnosticLogger& logger) {
  // First call: determine required buffer size
  size_t size = 0;
  if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0) {
    int err = errno;
    logger.Debug(
        "Failed to query sysctl size",
        {{"sysctl_name", name}, {"errno", static_cast<int64_t>(err)}}
    );
    return "";
  }

  // Second call: retrieve the actual value
  std::string result(size, '\0');
  if (sysctlbyname(name, result.data(), &size, nullptr, 0) != 0) {
    int err = errno;
    logger.Debug(
        "Failed to retrieve sysctl value",
        {{"sysctl_name", name}, {"errno", static_cast<int64_t>(err)}}
    );
    return "";
  }

  // Remove null terminator if present
  if (!result.empty() && result.back() == '\0') {
    result.pop_back();
  }

  return result;
}

/**
 * Parses the major version from a dot-delimited version string.
 *
 * @param version Full version string (e.g., "14.2.1")
 * @return Major version component (e.g., "14"), or "0" if parsing fails
 */
std::string ParseMajorVersion(const std::string& version) {
  if (version.empty()) {
    return "0";
  }

  size_t dot_pos = version.find('.');
  if (dot_pos == std::string::npos) {
    // No dot found; entire string is the major version
    return version;
  }

  return version.substr(0, dot_pos);
}

}  // namespace

/**
 * macOS implementation of ISystemInfo.
 *
 * Collects OS information using sysctlbyname for product version and build.
 */
class MacOSSystemInfo final : public ISystemInfo {
  OsInfo _os_info;

 public:
  explicit MacOSSystemInfo(impl::DiagnosticLogger& logger) {
    _os_info.name = "macOS";

    // Get product version (e.g., "14.2.1")
    _os_info.version = GetSysctlString("kern.osproductversion", logger);
    if (_os_info.version.empty()) {
      logger.Debug("Failed to retrieve macOS product version; using defaults");
      _os_info.version = "0";
      _os_info.version_major = "0";
      _os_info.build = "";
      return;
    }

    // Parse major version from product version
    _os_info.version_major = ParseMajorVersion(_os_info.version);

    // Get build version (e.g., "23C71")
    _os_info.build = GetSysctlString("kern.osversion", logger);
    if (_os_info.build.empty()) {
      logger.Debug("Failed to retrieve macOS build version");
      // Continue with empty build; version information is still valid
    }
  }

  const OsInfo& GetOsInfo() const override { return _os_info; }
};

std::unique_ptr<ISystemInfo> SystemInfo::Init(impl::DiagnosticLogger& logger) {
  return std::make_unique<MacOSSystemInfo>(logger);
}

}  // namespace datadog::platform
