// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <windows.h>

#include <string>

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/system_info.hpp"

namespace datadog::platform {

namespace {

typedef LONG(WINAPI* RtlGetVersionFunc)(OSVERSIONINFOEXW*);

/**
 * Retrieves Windows version information using RtlGetVersion.
 * This is the recommended approach as it bypasses GetVersionEx compatibility shims.
 *
 * @param info Output structure to populate
 * @return true if successful, false otherwise
 */
bool GetWindowsVersion(OSVERSIONINFOEXW& info) {
  // Load ntdll.dll and get RtlGetVersion function pointer
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) {
    return false;
  }

  auto RtlGetVersion =
      reinterpret_cast<RtlGetVersionFunc>(GetProcAddress(ntdll, "RtlGetVersion"));
  if (!RtlGetVersion) {
    return false;
  }

  info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
  return RtlGetVersion(&info) == 0;  // 0 indicates success (STATUS_SUCCESS)
}

/**
 * Retrieves the Windows Update Build Revision (UBR) from the registry.
 * UBR represents the latest cumulative update installed.
 *
 * @return UBR value, or 0 if unavailable
 */
DWORD GetWindowsUBR() {
  DWORD ubr = 0;
  DWORD size = sizeof(ubr);

  LSTATUS result = RegGetValueW(
      HKEY_LOCAL_MACHINE,
      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
      L"UBR",
      RRF_RT_DWORD,
      nullptr,
      &ubr,
      &size
  );

  return (result == ERROR_SUCCESS) ? ubr : 0;
}

}  // namespace

/**
 * Windows implementation of ISystemInfo.
 *
 * Collects OS information using RtlGetVersion and registry queries.
 * Requires Windows 7 or later.
 */
class WindowsSystemInfo final : public ISystemInfo {
  OsInfo _os_info;

 public:
  explicit WindowsSystemInfo(impl::DiagnosticLogger& logger) {
    OSVERSIONINFOEXW version_info = {};

    if (!GetWindowsVersion(version_info)) {
      logger.Debug(
          "Failed to retrieve Windows version information using RtlGetVersion"
      );
      // Use defaults
      _os_info.name = "Windows";
      _os_info.version = "0";
      _os_info.version_major = "0";
      _os_info.build = "";
      return;
    }

    // Get UBR (Update Build Revision) from registry
    DWORD ubr = GetWindowsUBR();

    // Format OS information
    _os_info.name = "Windows";
    _os_info.version_major = std::to_string(version_info.dwMajorVersion);

    // Build string: "build" or "build.ubr" if UBR is available
    if (ubr > 0) {
      _os_info.build =
          std::to_string(version_info.dwBuildNumber) + "." + std::to_string(ubr);
    } else {
      _os_info.build = std::to_string(version_info.dwBuildNumber);
    }

    // Version string: "major.minor.build" (with UBR included in build if available)
    _os_info.version = std::to_string(version_info.dwMajorVersion) + "." +
                       std::to_string(version_info.dwMinorVersion) + "." +
                       _os_info.build;
  }

  const OsInfo& GetOsInfo() const override { return _os_info; }
};

std::unique_ptr<ISystemInfo> SystemInfo::Init(impl::DiagnosticLogger& logger) {
  return std::make_unique<WindowsSystemInfo>(logger);
}

}  // namespace datadog::platform
