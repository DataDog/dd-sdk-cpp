// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <limits.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstdlib>
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

/**
 * Reads a single-line file from /sys/devices/virtual/dmi/id/.
 *
 * @param filename Path to the DMI file to read
 * @return File contents trimmed of whitespace, or empty on failure
 */
std::string ReadDmiFile(const char* filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    return "";
  }

  std::string result;
  std::getline(file, result);

  // Trim trailing whitespace
  while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) {
    result.pop_back();
  }

  return result;
}

/**
 * Parses Linux locale from environment variables.
 *
 * Checks LC_ALL first, then LANG. Strips encoding suffixes and converts
 * underscores to hyphens. Rejects non-language values like "C" and "POSIX".
 *
 * @param logger Diagnostic logger for warnings
 * @return Locale string (e.g., "en-US"), or empty on failure
 */
std::string ParseLinuxLocale(impl::DiagnosticLogger& logger) {
  // Check LC_ALL first, then LANG
  // NOTE: getenv is not thread-safe with concurrent *modifications* to environment
  // variables. This would technically be a race condition if the client application
  // decided to call putenv/unsetenv in another thread while concurrently initializing
  // the SDK, but that's very unlikely. The SDK guarantees that this code runs once,
  // synchronously, at SDK startup.
  const char* locale_env = std::getenv("LC_ALL");  // NOLINT(concurrency-mt-unsafe)
  if (!locale_env || locale_env[0] == '\0') {
    locale_env = std::getenv("LANG");  // NOLINT(concurrency-mt-unsafe)
  }

  if (!locale_env || locale_env[0] == '\0') {
    logger.Debug("No locale environment variables found (LC_ALL, LANG)");
    return "";
  }

  std::string locale = locale_env;

  // Reject non-language values
  if (locale == "C" || locale == "POSIX") {
    logger.Debug("Locale is non-language value, rejecting", {{"locale", locale}});
    return "";
  }

  // Strip .-delimited suffix (e.g., "en_US.UTF-8" -> "en_US")
  size_t dot_pos = locale.find('.');
  if (dot_pos != std::string::npos) {
    locale = locale.substr(0, dot_pos);
  }

  // Strip @-delimited suffix (e.g., "de_DE@euro" -> "de_DE")
  size_t at_pos = locale.find('@');
  if (at_pos != std::string::npos) {
    locale = locale.substr(0, at_pos);
  }

  // Replace _ with - (e.g., "en_US" -> "en-US")
  for (char& c : locale) {
    if (c == '_') {
      c = '-';
    }
  }

  return locale;
}

/**
 * Resolves the Linux timezone from /etc/localtime symlink.
 *
 * @param logger Diagnostic logger for warnings
 * @return IANA timezone (e.g., "America/New_York"), or empty on failure
 */
std::string GetLinuxTimezone(impl::DiagnosticLogger& logger) {
  char buffer[PATH_MAX];
  ssize_t len =
      readlink("/etc/localtime", static_cast<char*>(buffer), sizeof(buffer) - 1);
  if (len == -1) {
    int err = errno;
    logger.Debug(
        "Failed to read /etc/localtime symlink", {{"errno", static_cast<int64_t>(err)}}
    );
    return "";
  }

  buffer[len] = '\0';  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
  std::string tz_path{static_cast<const char*>(buffer)};

  // Strip /usr/share/zoneinfo/ prefix if present
  const std::string prefix = "/usr/share/zoneinfo/";
  if (tz_path.find(prefix) == 0) {
    return tz_path.substr(prefix.length());
  }

  // If prefix not found, return as-is (might be a different format)
  return tz_path;
}

}  // namespace

/**
 * Linux implementation of ISystemInfo.
 *
 * Collects OS and device information from /etc/os-release, uname(), DMI files, and env
 * vars.
 */
class LinuxSystemInfo final : public ISystemInfo {
  OsInfo _os_info;
  DeviceInfo _device_info;

 public:
  explicit LinuxSystemInfo(impl::DiagnosticLogger& logger) {
    // Collect OS information
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
      _os_info.build = static_cast<const char*>(uname_data.version);

      // Get architecture from uname as well (for device info)
      _device_info.architecture = static_cast<const char*>(uname_data.machine);
    } else {
      int err = errno;
      logger.Debug(
          "Failed to retrieve kernel version using uname",
          {{"errno", static_cast<int64_t>(err)}}
      );
      _os_info.build = "";
      _device_info.architecture = "";
    }

    // Collect device information
    _device_info.type = "desktop";

    // Read device info from DMI files
    _device_info.name = ReadDmiFile("/sys/devices/virtual/dmi/id/product_name");
    if (_device_info.name.empty()) {
      logger.Debug("Failed to read device name from DMI");
    }

    _device_info.model = ReadDmiFile("/sys/devices/virtual/dmi/id/product_version");
    if (_device_info.model.empty()) {
      logger.Debug("Failed to read device model from DMI");
    }

    _device_info.brand = ReadDmiFile("/sys/devices/virtual/dmi/id/sys_vendor");
    if (_device_info.brand.empty()) {
      logger.Debug("Failed to read device brand from DMI");
    }

    if (_device_info.architecture.empty()) {
      logger.Debug("Failed to retrieve device architecture");
    }

    // Parse locale from environment variables
    _device_info.locale = ParseLinuxLocale(logger);
    if (_device_info.locale.empty()) {
      logger.Debug("Failed to parse device locale");
    }

    // Get timezone from /etc/localtime
    _device_info.time_zone = GetLinuxTimezone(logger);
    if (_device_info.time_zone.empty()) {
      logger.Debug("Failed to resolve device timezone");
    }
  }

  const OsInfo& GetOsInfo() const override { return _os_info; }
  const DeviceInfo& GetDeviceInfo() const override { return _device_info; }
};

std::unique_ptr<ISystemInfo> SystemInfo::Init(impl::DiagnosticLogger& logger) {
  return std::make_unique<LinuxSystemInfo>(logger);
}

}  // namespace datadog::platform
