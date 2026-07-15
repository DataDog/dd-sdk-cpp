// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <limits.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

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
std::string ParseOsReleaseValue(std::string_view line, std::string_view key) {
  // Check if line starts with the key
  if (line.find(key) != 0) {
    return "";
  }

  // Find the equals sign
  size_t equals_pos = line.find('=');
  if (equals_pos == std::string_view::npos || equals_pos != key.length()) {
    return "";
  }

  // Extract value after '='
  std::string_view value = line.substr(equals_pos + 1);

  // Remove surrounding quotes if present
  if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
    value = value.substr(1, value.length() - 2);
  }

  return std::string(value);
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
    std::string& name, std::string& version, const impl::DiagnosticLogger& logger
) {
  std::ifstream file("/etc/os-release");
  if (!file.is_open()) {
    logger.Debug(
        "Unable to resolve distribution information for OS name and version: failed to "
        "open /etc/os-release"
    );
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

  // Trim trailing whitespace - find position, then resize once
  size_t end = result.length();
  while (end > 0 && std::isspace(static_cast<unsigned char>(result[end - 1]))) {
    --end;
  }
  result.resize(end);

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
std::string ParseLinuxLocale(const impl::DiagnosticLogger& logger) {
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
    logger.Debug(
        "Unable to resolve device locale: no locale environment variables found "
        "(LC_ALL, LANG)"
    );
    return "";
  }

  std::string locale = locale_env;

  // Reject non-language values
  if (locale == "C" || locale == "POSIX") {
    logger.Debug(
        "Unable to resolve device locale: nonstandard LANG value rejected",
        {{"locale", locale}}
    );
    return "";
  }

  // Strip .-delimited suffix (e.g., "en_US.UTF-8" -> "en_US")
  size_t dot_pos = locale.find('.');
  if (dot_pos != std::string::npos) {
    locale.resize(dot_pos);
  }

  // Strip @-delimited suffix (e.g., "de_DE@euro" -> "de_DE")
  size_t at_pos = locale.find('@');
  if (at_pos != std::string::npos) {
    locale.resize(at_pos);
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
std::string GetLinuxTimezone(const impl::DiagnosticLogger& logger) {
  char buffer[PATH_MAX];
  ssize_t len =
      readlink("/etc/localtime", static_cast<char*>(buffer), sizeof(buffer) - 1);
  if (len == -1) {
    int err = errno;
    logger.Debug(
        "Unable to resolve device timezone: failed to read /etc/localtime symlink",
        {{"errno", static_cast<int64_t>(err)}}
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

  // If prefix not found, we just have a mystery path that we can't confidently treat as
  // a timezone, so ignore it
  logger.Debug(
      "Unable to resolve device timezone: /etc/localtime symlink does not point to a "
      "path in /usr/share/zoneinfo/",
      {{"path", tz_path}}
  );
  return "";
}

/**
 * Parses the starttime field (field 22, zero-indexed field 21) from /proc/self/stat.
 *
 * /proc/self/stat fields are space-separated. The second field is the process name
 * enclosed in parentheses and may itself contain spaces, so we skip past the closing
 * parenthesis before counting fields.
 *
 * @param logger Diagnostic logger for warnings
 * @return starttime in clock ticks since boot, or 0 on failure
 */
uint64_t ReadProcStatStarttime(const impl::DiagnosticLogger& logger) {
  std::ifstream file("/proc/self/stat");
  if (!file.is_open()) {
    logger.Debug(
        "Unable to resolve process launch time: failed to open /proc/self/stat"
    );
    return 0;
  }

  std::string content;
  std::getline(file, content);

  // Skip past the closing ')' of the comm field (field 2)
  size_t rparen = content.rfind(')');
  if (rparen == std::string::npos) {
    logger.Debug(
        "Unable to resolve process launch time: malformed /proc/self/stat (no closing "
        "parenthesis)"
    );
    return 0;
  }

  // Fields after comm are space-separated starting at rparen+1.
  // starttime is field 22 (1-indexed). Fields 3..21 must be skipped (19 fields)
  // before reading field 22.
  std::string_view remaining(content.c_str() + rparen + 1, content.size() - rparen - 1);
  int fields_needed = 19;
  size_t pos = 0;
  for (int i = 0; i < fields_needed; ++i) {
    // Skip leading spaces
    while (pos < remaining.size() && remaining[pos] == ' ') {
      ++pos;
    }
    // Skip the field value
    while (pos < remaining.size() && remaining[pos] != ' ') {
      ++pos;
    }
  }

  // Skip leading spaces before starttime
  while (pos < remaining.size() && remaining[pos] == ' ') {
    ++pos;
  }

  if (pos >= remaining.size()) {
    logger.Debug(
        "Unable to resolve process launch time: /proc/self/stat has fewer fields than "
        "expected"
    );
    return 0;
  }

  // Parse starttime as uint64
  uint64_t starttime = 0;
  bool parsed_any = false;
  while (pos < remaining.size() && remaining[pos] >= '0' && remaining[pos] <= '9') {
    starttime = starttime * 10 + static_cast<uint64_t>(remaining[pos] - '0');
    ++pos;
    parsed_any = true;
  }

  if (!parsed_any) {
    logger.Debug(
        "Unable to resolve process launch time: failed to parse starttime field in "
        "/proc/self/stat"
    );
    return 0;
  }

  return starttime;
}

/**
 * Retrieves the process launch time on Linux.
 *
 * Reads starttime (clock ticks since boot) from /proc/self/stat, converts to
 * nanoseconds using sysconf(_SC_CLK_TCK), then adds to the wall-clock boot time
 * derived from back-to-back CLOCK_BOOTTIME and CLOCK_REALTIME samples.
 *
 * @param logger Diagnostic logger for warnings
 * @return Wall-clock launch time as a Timestamp, or zero on failure
 */
Timestamp QueryProcessLaunchTime(const impl::DiagnosticLogger& logger) {
  // Read starttime from /proc/self/stat
  uint64_t starttime_ticks = ReadProcStatStarttime(logger);
  if (starttime_ticks == 0) {
    return Timestamp{};
  }

  // Get clock tick rate
  int64_t clk_tck = static_cast<int64_t>(sysconf(_SC_CLK_TCK));
  if (clk_tck <= 0) {
    logger.Debug(
        "Unable to resolve process launch time: sysconf(_SC_CLK_TCK) returned invalid "
        "value",
        {{"clk_tck", clk_tck}}
    );
    return Timestamp{};
  }

  // Sample CLOCK_BOOTTIME and CLOCK_REALTIME back-to-back to minimize drift between
  // the two readings, then use their difference to derive the wall-clock boot time.
  struct timespec boottime_ts{};
  struct timespec realtime_ts{};
  if (clock_gettime(CLOCK_BOOTTIME, &boottime_ts) != 0) {
    int err = errno;
    logger.Debug(
        "Unable to resolve process launch time: clock_gettime(CLOCK_BOOTTIME) failed",
        {{"errno", static_cast<int64_t>(err)}}
    );
    return Timestamp{};
  }
  if (clock_gettime(CLOCK_REALTIME, &realtime_ts) != 0) {
    int err = errno;
    logger.Debug(
        "Unable to resolve process launch time: clock_gettime(CLOCK_REALTIME) failed",
        {{"errno", static_cast<int64_t>(err)}}
    );
    return Timestamp{};
  }

  // Compute wall-clock boot time: realtime - boottime (both in nanoseconds)
  int64_t realtime_ns = (realtime_ts.tv_sec * 1'000'000'000LL) + realtime_ts.tv_nsec;
  int64_t boottime_ns = (boottime_ts.tv_sec * 1'000'000'000LL) + boottime_ts.tv_nsec;
  int64_t boot_epoch_ns = realtime_ns - boottime_ns;

  // Convert starttime from ticks to nanoseconds since boot.
  // Divide into whole seconds and a remainder first to avoid overflowing int64_t
  // when multiplying ticks by 1e9 before the division.
  int64_t ticks = static_cast<int64_t>(starttime_ticks);
  int64_t whole_sec = ticks / clk_tck;
  int64_t rem_ticks = ticks % clk_tck;
  int64_t starttime_ns =
      whole_sec * 1'000'000'000LL + rem_ticks * 1'000'000'000LL / clk_tck;

  return Timestamp{Duration{boot_epoch_ns + starttime_ns}};
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
  Timestamp _process_launch_time;

 public:
  explicit LinuxSystemInfo(const impl::DiagnosticLogger& logger) {
    // Collect OS information
    // Parse /etc/os-release for name and version
    if (!ParseOsRelease(_os_info.name, _os_info.version, logger)) {
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
          "Unable resolve OS build and device architecture: uname failed",
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
      logger.Debug(
          "Unable to resolve device name: failed to read from "
          "/sys/devices/virtual/dmi/id/product_name"
      );
    }

    _device_info.model = ReadDmiFile("/sys/devices/virtual/dmi/id/product_version");
    if (_device_info.model.empty()) {
      logger.Debug(
          "Unable to resolve device model: failed to read from "
          "/sys/devices/virtual/dmi/id/product_version"
      );
    }

    _device_info.brand = ReadDmiFile("/sys/devices/virtual/dmi/id/sys_vendor");
    if (_device_info.brand.empty()) {
      logger.Debug(
          "Unable to resolve device brand: failed to read from "
          "/sys/devices/virtual/dmi/id/sys_vendor"
      );
    }

    // Parse locale from environment variables
    _device_info.locale = ParseLinuxLocale(logger);

    // Get timezone from /etc/localtime
    _device_info.time_zone = GetLinuxTimezone(logger);

    // Get process launch time
    _process_launch_time = QueryProcessLaunchTime(logger);
  }

  int64_t GetPid() const override { return static_cast<int64_t>(getpid()); }
  const OsInfo& GetOsInfo() const override { return _os_info; }
  const DeviceInfo& GetDeviceInfo() const override { return _device_info; }
  Timestamp GetProcessLaunchTime() const override { return _process_launch_time; }
};

std::unique_ptr<ISystemInfo> SystemInfo::Init(const impl::DiagnosticLogger& logger) {
  return std::make_unique<LinuxSystemInfo>(logger);
}

}  // namespace datadog::platform
