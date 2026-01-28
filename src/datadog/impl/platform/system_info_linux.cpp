// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <sys/utsname.h>

#include <fstream>
#include <string>

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/system_info.hpp"

namespace datadog::platform {

namespace {

/**
 * Parses a key-value pair from /etc/os-release format.
 * Format: KEY="value" or KEY=value
 *
 * @param line Line to parse
 * @param key Key to search for
 * @return Value if key matches, empty string otherwise
 */
std::string ParseOsReleaseValue(const std::string& line, const std::string& key) {
  // Check if line starts with the key
  if (line.find(key) != 0) {
    return "";
  }

  // Find the equals sign
  size_t equals_pos = line.find('=');
  if (equals_pos == std::string::npos || equals_pos != key.length()) {
    return "";
  }

  // Extract value after '='
  std::string value = line.substr(equals_pos + 1);

  // Remove surrounding quotes if present
  if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.length() - 2);
  }

  return value;
}

/**
 * Parses /etc/os-release to extract distribution information.
 *
 * @param name Output: OS name (from NAME, or "Linux" if unavailable)
 * @param version Output: OS version (from VERSION_ID, or "0" if unavailable)
 * @param logger Diagnostic logger for warnings
 * @return true if file was successfully parsed
 */
bool ParseOsRelease(
    std::string& name, std::string& version, impl::DiagnosticLogger& logger
) {
  std::ifstream file("/etc/os-release");
  if (!file.is_open()) {
    logger.Debug("Failed to open /etc/os-release");
    return false;
  }

  std::string line;

  while (std::getline(file, line)) {
    // Try to parse NAME
    std::string parsed_name = ParseOsReleaseValue(line, "NAME");
    if (!parsed_name.empty()) {
      name = parsed_name;
    }

    // Try to parse VERSION_ID
    std::string parsed_version = ParseOsReleaseValue(line, "VERSION_ID");
    if (!parsed_version.empty()) {
      version = parsed_version;
    }
  }

  // Fallback logic for name
  if (name.empty()) {
    name = "Linux";
  }

  // Default version if not found
  if (version.empty()) {
    version = "0";
  }

  return true;
}

/**
 * Parses the major version from a dot-delimited version string.
 *
 * @param version Full version string (e.g., "22.04")
 * @return Major version component (e.g., "22"), or "0" if parsing fails
 */
std::string ParseMajorVersion(const std::string& version) {
  if (version.empty() || version == "0") {
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
 * Linux implementation of ISystemInfo.
 *
 * Collects OS information from /etc/os-release and uname().
 */
class LinuxSystemInfo final : public ISystemInfo {
  OsInfo _os_info;

 public:
  explicit LinuxSystemInfo(impl::DiagnosticLogger& logger) {
    // Parse /etc/os-release for name and version
    if (!ParseOsRelease(_os_info.name, _os_info.version, logger)) {
      logger.Debug("Failed to parse /etc/os-release; using defaults");
      _os_info.name = "Linux";
      _os_info.version = "0";
    }

    // Parse major version
    _os_info.version_major = ParseMajorVersion(_os_info.version);

    // Get kernel build string using uname
    struct utsname uname_data = {};
    if (uname(&uname_data) == 0) {
      _os_info.build = uname_data.version;
    } else {
      int err = errno;
      logger.Debug(
          "Failed to retrieve kernel version using uname",
          {{"errno", static_cast<int64_t>(err)}}
      );
      _os_info.build = "";
    }
  }

  const OsInfo& GetOsInfo() const override { return _os_info; }
};

std::unique_ptr<ISystemInfo> SystemInfo::Init(impl::DiagnosticLogger& logger) {
  return std::make_unique<LinuxSystemInfo>(logger);
}

}  // namespace datadog::platform
