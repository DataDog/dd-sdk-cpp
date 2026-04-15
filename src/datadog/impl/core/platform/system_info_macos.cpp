// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <CoreFoundation/CoreFoundation.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <string>

#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog::platform {

namespace {

/**
 * Retrieves a string value from sysctl by name.
 *
 * @param name sysctl variable name (e.g., "kern.osproductversion")
 * @return Retrieved string value, or empty string on failure
 */
std::string GetSysctlString(const char* name) {
  // First call: determine required buffer size
  size_t size = 0;
  if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0) {
    return "";
  }

  // Second call: retrieve the actual value
  std::string result(size, '\0');
  if (sysctlbyname(name, result.data(), &size, nullptr, 0) != 0) {
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
std::string ParseMajorVersion(std::string_view version) {
  if (version.empty()) {
    return "0";
  }

  size_t dot_pos = version.find('.');
  if (dot_pos == std::string_view::npos) {
    // No dot found; entire string is the major version
    return std::string(version);
  }

  return std::string(version.substr(0, dot_pos));
}

/**
 * Extracts the alphabetical device name prefix from a model string.
 *
 * For example, "MacBookPro16,1" -> "MacBookPro"
 *
 * @param model Model string from hw.model
 * @return Alphabetical prefix, or empty string if model is empty
 */
std::string ExtractDeviceName(std::string_view model) {
  if (model.empty()) {
    return "";
  }

  // Find the first non-alphabetic character
  size_t end = 0;
  while (end < model.size() && std::isalpha(static_cast<unsigned char>(model[end]))) {
    ++end;
  }

  return std::string(model.substr(0, end));
}

/**
 * Retrieves the user's current locale using CoreFoundation.
 *
 * @param logger Diagnostic logger for warnings
 * @return Locale string in format "en-US", or empty on failure
 */
std::string GetUserLocale(impl::DiagnosticLogger& logger) {
  CFLocaleRef locale = CFLocaleCopyCurrent();
  if (!locale) {
    logger.Debug("Unable to resolve device locale: CFLocaleCopyCurrent failed");
    return "";
  }

  CFStringRef locale_id =
      static_cast<CFStringRef>(CFLocaleGetValue(locale, kCFLocaleIdentifier));
  if (!locale_id) {
    CFRelease(locale);
    logger.Debug(
        "Unable to resolve device locale: failed to get locale identifier from CFLocale"
    );
    return "";
  }

  // Get C string from CFString
  const char* c_str = CFStringGetCStringPtr(locale_id, kCFStringEncodingUTF8);
  std::string result;
  if (c_str) {
    result = c_str;
  } else {
    // Need to use CFStringGetCString for non-ASCII or complex strings
    CFIndex length = CFStringGetLength(locale_id);
    CFIndex max_size =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string buffer(max_size, '\0');
    if (CFStringGetCString(locale_id, buffer.data(), max_size, kCFStringEncodingUTF8)) {
      // Remove null terminator - resize in place
      size_t actual_length = strlen(buffer.c_str());
      buffer.resize(actual_length);
      result = std::move(buffer);
    }
  }

  CFRelease(locale);

  // Convert underscore to hyphen (e.g., "en_US" -> "en-US")
  for (char& c : result) {
    if (c == '_') {
      c = '-';
    }
  }

  return result;
}

/**
 * Retrieves the system timezone using CoreFoundation.
 *
 * @param logger Diagnostic logger for warnings
 * @return IANA timezone name (e.g., "America/Halifax"), or empty on failure
 */
std::string GetSystemTimezone(impl::DiagnosticLogger& logger) {
  CFTimeZoneRef tz = CFTimeZoneCopySystem();
  if (!tz) {
    logger.Debug("Unable to resolve device timezone: CFTimeZoneCopySystem failed");
    return "";
  }

  CFStringRef tz_name = CFTimeZoneGetName(tz);
  if (!tz_name) {
    CFRelease(tz);
    logger.Debug(
        "Unable to resolve device timezone: failed to get timezone name from CFTimeZone"
    );
    return "";
  }

  // Get C string from CFString
  const char* c_str = CFStringGetCStringPtr(tz_name, kCFStringEncodingUTF8);
  std::string result;
  if (c_str) {
    result = c_str;
  } else {
    // Need to use CFStringGetCString for non-ASCII or complex strings
    CFIndex length = CFStringGetLength(tz_name);
    CFIndex max_size =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string buffer(max_size, '\0');
    if (CFStringGetCString(tz_name, buffer.data(), max_size, kCFStringEncodingUTF8)) {
      // Remove null terminator - resize in place
      size_t actual_length = strlen(buffer.c_str());
      buffer.resize(actual_length);
      result = std::move(buffer);
    }
  }

  CFRelease(tz);
  return result;
}

/**
 * Retrieves the CPU architecture using uname.
 *
 * @param logger Diagnostic logger for warnings
 * @return Architecture string (e.g., "arm64", "x86_64"), or empty on failure
 */
std::string GetArchitecture(impl::DiagnosticLogger& logger) {
  struct utsname uts{};
  if (uname(&uts) != 0) {
    int err = errno;
    logger.Debug(
        "Unable to resolve device architecture: uname failed",
        {{"errno", static_cast<int64_t>(err)}}
    );
    return "";
  }

  return static_cast<const char*>(uts.machine);
}

}  // namespace

/**
 * macOS implementation of ISystemInfo.
 *
 * Collects OS and device information using sysctlbyname, uname, and CoreFoundation
 * APIs.
 */
class MacOSSystemInfo final : public ISystemInfo {
  OsInfo _os_info;
  DeviceInfo _device_info;

 public:
  explicit MacOSSystemInfo(impl::DiagnosticLogger& logger) {
    // Collect OS information
    _os_info.name = "macOS";

    // Get product version (e.g., "14.2.1")
    _os_info.version = GetSysctlString("kern.osproductversion");
    if (_os_info.version.empty()) {
      logger.Debug(
          "Unable to resolve OS version: failed to get kern.osproductversion from "
          "sysctl"
      );
      _os_info.version = "0";
      _os_info.version_major = "0";
      _os_info.build = "";
    } else {
      // Parse major version from product version
      _os_info.version_major = ParseMajorVersion(_os_info.version);

      // Get build version (e.g., "23C71")
      _os_info.build = GetSysctlString("kern.osversion");
      if (_os_info.build.empty()) {
        logger.Debug(
            "Unable to resolve OS version: failed to get kern.osversion from sysctl"
        );
      }
    }

    // Collect device information
    _device_info.type = "desktop";
    _device_info.brand = "Apple";

    // Get model (e.g., "MacBookPro16,1")
    _device_info.model = GetSysctlString("hw.model");
    if (_device_info.model.empty()) {
      logger.Debug(
          "Unable to resolve device model: failed to get hw.model from sysctl"
      );
    }

    // Extract device name from model (e.g., "MacBookPro16,1" -> "MacBookPro")
    _device_info.name = ExtractDeviceName(_device_info.model);

    // Get architecture (e.g., "arm64", "x86_64")
    _device_info.architecture = GetArchitecture(logger);

    // Get user locale (e.g., "en-US")
    _device_info.locale = GetUserLocale(logger);

    // Get timezone (e.g., "America/Halifax")
    _device_info.time_zone = GetSystemTimezone(logger);
  }

  int64_t GetPid() const override { return static_cast<int64_t>(getpid()); }
  const OsInfo& GetOsInfo() const override { return _os_info; }
  const DeviceInfo& GetDeviceInfo() const override { return _device_info; }
};

std::unique_ptr<ISystemInfo> SystemInfo::Init(impl::DiagnosticLogger& logger) {
  return std::make_unique<MacOSSystemInfo>(logger);
}

}  // namespace datadog::platform
