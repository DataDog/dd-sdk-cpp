// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <comdef.h>
#include <wbemidl.h>
#include <windows.h>

#include <algorithm>
#include <string>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/system_info.hpp"
#include "datadog/impl/platform/windows_timezone_mapping.hpp"

#pragma comment(lib, "wbemuuid.lib")

namespace datadog::platform {

/**
 * Maps a Windows timezone name to an IANA timezone ID.
 *
 * Uses binary search on the sorted WINDOWS_TIMEZONE_LOOKUP to find the IANA equivalent.
 * If no mapping is found, returns the Windows timezone name as-is.
 *
 * @param windows_tz Windows timezone name (e.g., "Pacific Standard Time")
 * @return IANA timezone ID (e.g., "America/Los_Angeles") or original name if not found
 */
std::string_view MapWindowsTimezoneToIANA(std::string_view windows_tz) {
  if (windows_tz.empty()) {
    return "";
  }

  // Define a string comparison function for our binary search
  auto comparator = [](const WindowsTimezoneMapping& mapping, std::string_view value) {
    return mapping.windows_tz < value;
  };

  // Run a binary search in our sorted timezone table, using std::lower_bound to return
  // the first element that's equal to or greater than our search value
  const auto* begin = WINDOWS_TIMEZONE_LOOKUP;
  const auto* end = begin + std::size(WINDOWS_TIMEZONE_LOOKUP);
  auto it = std::lower_bound(begin, end, windows_tz, comparator);

  // If we matched an exact Windows timezone name value, return the corresponding IANA
  // value
  if (it != end && windows_tz == it->windows_tz) {
    return it->iana_tz;
  }

  // If we found no match for the given Windows timezone name, return the input value
  // unchanged
  return windows_tz;
}

namespace {

typedef LONG(WINAPI* RtlGetVersionFunc)(OSVERSIONINFOEXW*);

/**
 * Retrieves Windows version information using RtlGetVersion.
 * This is the recommended approach as it bypasses GetVersionEx compatibility shims.
 *
 * @param info Output structure to populate
 * @param logger Logger for emitting debug messages on failure
 * @return true if successful, false otherwise
 */
bool GetWindowsVersion(OSVERSIONINFOEXW& info, impl::DiagnosticLogger& logger) {
  // Load ntdll.dll and get RtlGetVersion function pointer
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) {
    logger.Debug("Unable to resolve OS version: failed to resolve ntdll.dll");
    return false;
  }

  auto RtlGetVersion =
      reinterpret_cast<RtlGetVersionFunc>(GetProcAddress(ntdll, "RtlGetVersion"));
  if (!RtlGetVersion) {
    logger.Debug(
        "Unable to resolve OS version: failed to resolve RtlGetVersion in ntdll.dll"
    );
    return false;
  }

  info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
  if (RtlGetVersion(&info) != ERROR_SUCCESS) {
    logger.Debug("Unable to resolve OS version: RtlGetVersion failed");
    return false;
  }
  return true;
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

/**
 * Converts a wide string (UTF-16) to a UTF-8 string.
 *
 * @param wstr Wide string to convert
 * @return UTF-8 encoded string
 */
std::string WideToUtf8(const std::wstring& wstr) {
  if (wstr.empty()) {
    return "";
  }

  int size_needed = WideCharToMultiByte(
      CP_UTF8,
      0,
      wstr.data(),
      static_cast<int>(wstr.size()),
      nullptr,
      0,
      nullptr,
      nullptr
  );
  if (size_needed <= 0) {
    return "";
  }

  std::string result(size_needed, '\0');
  WideCharToMultiByte(
      CP_UTF8,
      0,
      wstr.data(),
      static_cast<int>(wstr.size()),
      result.data(),
      size_needed,
      nullptr,
      nullptr
  );

  return result;
}

/**
 * RAII wrapper for WMI queries that manages COM lifecycle.
 * Allows multiple queries with a single COM initialization/teardown cycle.
 */
class WmiQueryContext {
  IWbemLocator* locator_ = nullptr;
  IWbemServices* services_ = nullptr;
  impl::DiagnosticLogger& logger_;
  bool initialized_ = false;
  bool com_initialized_ = false;

 public:
  explicit WmiQueryContext(impl::DiagnosticLogger& logger) : logger_(logger) {}

  ~WmiQueryContext() {
    if (services_) {
      services_->Release();
    }
    if (locator_) {
      locator_->Release();
    }
    if (com_initialized_) {
      CoUninitialize();
    }
  }

  /**
   * Initialize COM and connect to WMI.
   * Must be called before QueryStringValue().
   *
   * @return true if initialization succeeded, false otherwise
   */
  bool Init() {
    HRESULT hr;

    // Init() should only be called once for any given WmiQueryContext
    if (initialized_) {
      DATADOG_ASSERT(false, "WmiQueryContext::Init() called twice");
      return false;
    }

    // Initialize COM
    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == S_OK) {
      // We successfully initialized COM, we own it and must call CoUninitialize
      com_initialized_ = true;
    } else if (hr == S_FALSE) {
      // COM already initialized with compatible settings (MTA), don't uninitialize
      // later
      com_initialized_ = false;
    } else if (hr == RPC_E_CHANGED_MODE) {
      // COM initialized with different apartment model (probably STA on UI thread)
      // This is fine, we can still use COM on this thread
      com_initialized_ = false;
    } else {
      // Actual failure
      logger_.Debug(
          "Failed to initialize COM for WMI query",
          {{"hresult", static_cast<int64_t>(hr)}}
      );
      return false;
    }

    // Initialize COM security
    hr = CoInitializeSecurity(
        nullptr,
        -1,
        nullptr,
        nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE,
        nullptr
    );
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
      logger_.Debug(
          "Failed to initialize COM security for WMI query",
          {{"hresult", static_cast<int64_t>(hr)}}
      );
      return false;
    }

    // Create WMI locator
    hr = CoCreateInstance(
        CLSID_WbemLocator,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator,
        reinterpret_cast<void**>(&locator_)
    );
    if (FAILED(hr)) {
      logger_.Debug(
          "Failed to create WMI locator", {{"hresult", static_cast<int64_t>(hr)}}
      );
      return false;
    }

    // Connect to WMI
    hr = locator_->ConnectServer(
        _bstr_t(L"ROOT\\CIMV2"),
        nullptr,
        nullptr,
        nullptr,
        0,
        nullptr,
        nullptr,
        &services_
    );
    if (FAILED(hr)) {
      logger_.Debug(
          "Failed to connect to WMI", {{"hresult", static_cast<int64_t>(hr)}}
      );
      return false;
    }

    // Set security on the proxy
    hr = CoSetProxyBlanket(
        services_,
        RPC_C_AUTHN_WINNT,
        RPC_C_AUTHZ_NONE,
        nullptr,
        RPC_C_AUTHN_LEVEL_CALL,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr,
        EOAC_NONE
    );
    if (FAILED(hr)) {
      logger_.Debug(
          "Failed to set WMI proxy blanket", {{"hresult", static_cast<int64_t>(hr)}}
      );
      return false;
    }

    initialized_ = true;
    return true;
  }

  /**
   * Query a WMI property value.
   * Init() must be called successfully before calling this method.
   *
   * @param wmi_class WMI class name (e.g., L"Win32_ComputerSystem")
   * @param property Property name (e.g., L"Manufacturer")
   * @return Property value as UTF-8 string, or empty on failure
   */
  std::string QueryStringValue(const wchar_t* wmi_class, const wchar_t* property) {
    if (!initialized_) {
      return "";
    }

    HRESULT hr;
    IEnumWbemClassObject* enumerator = nullptr;  // Must be released once initialized

    // Execute WMI query, populating enumerator on success
    std::wstring query;
    query.reserve(8 + wcslen(property) + 6 + wcslen(wmi_class));
    query = L"SELECT ";
    query += property;
    query += L" FROM ";
    query += wmi_class;
    hr = services_->ExecQuery(
        _bstr_t(L"WQL"),
        _bstr_t(query.c_str()),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr,
        &enumerator
    );
    if (FAILED(hr)) {
      DATADOG_ASSERT(
          enumerator == nullptr,
          "IWbemServices::ExecQuery returned valid enumerator on failure"
      );
      logger_.Debug(
          "Failed to execute WMI query", {{"hresult", static_cast<int64_t>(hr)}}
      );
      return "";
    }

    // Use the enumerator to get the first result
    IWbemClassObject* obj = nullptr;
    ULONG returned = 0;
    hr = enumerator->Next(WBEM_INFINITE, 1, &obj, &returned);
    if (FAILED(hr) || returned == 0) {
      enumerator->Release();
      DATADOG_ASSERT(
          obj == nullptr, "IWbemClassObject::Next returned valid obj on failure"
      );
      return "";
    }

    // Get the property value
    std::string result;
    VARIANT variant;
    VariantInit(&variant);
    hr = obj->Get(property, 0, &variant, nullptr, nullptr);
    if (SUCCEEDED(hr) && variant.vt == VT_BSTR && variant.bstrVal) {
      result = WideToUtf8(variant.bstrVal);
    }

    // Cleanup
    VariantClear(&variant);
    obj->Release();
    enumerator->Release();

    // Return value as string
    return result;
  }
};

/**
 * Maps Windows processor architecture to string representation.
 *
 * @param arch Processor architecture from SYSTEM_INFO
 * @return Architecture string (e.g., "x86", "x86_64", "arm64")
 */
std::string MapProcessorArchitecture(WORD arch) {
  switch (arch) {
    case PROCESSOR_ARCHITECTURE_INTEL:
      return "x86";
    case PROCESSOR_ARCHITECTURE_AMD64:
      return "x86_64";
    case PROCESSOR_ARCHITECTURE_ARM64:
      return "arm64";
    default:
      return "";
  }
}

/**
 * Retrieves the device locale using GetUserDefaultLocaleName.
 *
 * @param logger Diagnostic logger for warnings
 * @return Locale string (e.g., "en-US"), or empty on failure
 */
std::string GetDeviceLocale(impl::DiagnosticLogger& logger) {
  wchar_t locale_name[LOCALE_NAME_MAX_LENGTH];
  int result = GetUserDefaultLocaleName(locale_name, LOCALE_NAME_MAX_LENGTH);
  if (result == 0) {
    DWORD err = GetLastError();
    logger.Debug(
        "Unable to resolve device locale: GetUserDefaultLocaleName failed",
        {{"error", static_cast<int64_t>(err)}}
    );
    return "";
  }

  return WideToUtf8(locale_name);
}

/**
 * Maps a Windows timezone name to an IANA timezone ID.
 *
 * Uses binary search on the sorted WINDOWS_TIMEZONE_LOOKUP to find the IANA equivalent.
 * If no mapping is found, returns the Windows timezone name as-is.
 *
 * @param windows_tz Windows timezone name (e.g., "Pacific Standard Time")
 * @return IANA timezone ID (e.g., "America/Los_Angeles") or original name if not found
 */
std::string_view MapWindowsTimezoneToIANA(std::string_view windows_tz) {
  if (windows_tz.empty()) {
    return "";
  }

  // Define a string comparison function for our binary search
  auto comparator = [](const impl::platform::WindowsTimezoneMapping& mapping,
                       std::string_view value) { return mapping.windows_tz < value; };

  // Run a binary search in our sorted timezone table, using std::lower_bound to return
  // the first element that's equal to or greater than our search value
  const auto* begin = impl::platform::WINDOWS_TIMEZONE_LOOKUP;
  const auto* end = begin + std::size(impl::platform::WINDOWS_TIMEZONE_LOOKUP);
  auto it = std::lower_bound(begin, end, windows_tz, comparator);

  // If we matched an exact Windows timezone name value, return the corresponding IANA
  // value
  if (it != end && windows_tz == it->windows_tz) {
    return it->iana_tz;
  }

  // If we found no match for the given Windows timezone name, return the input value
  // unchanged
  return windows_tz;
}

/**
 * Retrieves the system timezone using GetDynamicTimeZoneInformation.
 *
 * Returns IANA timezone ID by mapping Windows timezone name via CLDR mapping table.
 *
 * @param logger Diagnostic logger for warnings
 * @return IANA timezone ID (e.g., "America/Los_Angeles"), or empty on failure
 */
std::string GetDeviceTimezone(impl::DiagnosticLogger& logger) {
  DYNAMIC_TIME_ZONE_INFORMATION tz_info;
  DWORD result = GetDynamicTimeZoneInformation(&tz_info);

  if (result == TIME_ZONE_ID_INVALID) {
    DWORD err = GetLastError();
    logger.Debug(
        "Unable to resolve device timezone: GetDynamicTimeZoneInformation failed",
        {{"error", static_cast<int64_t>(err)}}
    );
    return "";
  }

  // Prefer TimeZoneKeyName if available, otherwise use StandardName
  std::wstring tz_name;
  if (tz_info.TimeZoneKeyName[0] != L'\0') {
    tz_name = tz_info.TimeZoneKeyName;
  } else if (tz_info.StandardName[0] != L'\0') {
    tz_name = tz_info.StandardName;
  } else {
    logger.Debug(
        "Unable to resolve device timezone: no timezone name available from "
        "GetDynamicTimeZoneInformation"
    );
    return "";
  }

  // Convert Windows timezone name to UTF-8 and map to IANA
  std::string windows_tz_utf8 = WideToUtf8(tz_name);
  return std::string(MapWindowsTimezoneToIANA(windows_tz_utf8));
}

}  // namespace

/**
 * Windows implementation of ISystemInfo.
 *
 * Collects OS and device information using RtlGetVersion, WMI queries, and Win32 APIs.
 * Requires Windows 7 or later.
 */
class WindowsSystemInfo final : public ISystemInfo {
  OsInfo _os_info;
  DeviceInfo _device_info;

 public:
  explicit WindowsSystemInfo(impl::DiagnosticLogger& logger) {
    // Collect OS information
    OSVERSIONINFOEXW version_info = {};

    if (!GetWindowsVersion(version_info, logger)) {
      // Use defaults
      _os_info.name = "Windows";
      _os_info.version = "0";
      _os_info.version_major = "0";
      _os_info.build = "";
    } else {
      // Get UBR (Update Build Revision) from registry
      DWORD ubr = GetWindowsUBR();

      // Format OS information
      _os_info.name = "Windows";
      _os_info.version_major = std::to_string(version_info.dwMajorVersion);

      // Build string: "build" or "build.ubr" if UBR is available
      _os_info.build.reserve(20);  // Conservative estimate for build numbers
      if (ubr > 0) {
        _os_info.build = std::to_string(version_info.dwBuildNumber);
        _os_info.build += '.';
        _os_info.build += std::to_string(ubr);
      } else {
        _os_info.build = std::to_string(version_info.dwBuildNumber);
      }

      // Version string: "major.minor.build" (with UBR included in build if available)
      _os_info.version.reserve(30);  // Conservative estimate for full version string
      _os_info.version = std::to_string(version_info.dwMajorVersion);
      _os_info.version += '.';
      _os_info.version += std::to_string(version_info.dwMinorVersion);
      _os_info.version += '.';
      _os_info.version += _os_info.build;
    }

    // Collect device information
    _device_info.type = "desktop";

    // Query WMI for device information
    WmiQueryContext wmi(logger);
    if (wmi.Init()) {
      _device_info.name = wmi.QueryStringValue(L"Win32_ComputerSystemProduct", L"Name");
      if (_device_info.name.empty()) {
        logger.Debug(
            "Unable to resolve device name: got no value from WMI query for "
            "Win32_ComputerSystemProduct.Name"
        );
      }

      _device_info.model = wmi.QueryStringValue(L"Win32_ComputerSystem", L"Model");
      if (_device_info.model.empty()) {
        logger.Debug(
            "Unable to resolve device model: got no value from WMI query for "
            "Win32_ComputerSystem.Model"
        );
      }

      _device_info.brand =
          wmi.QueryStringValue(L"Win32_ComputerSystem", L"Manufacturer");
      if (_device_info.brand.empty()) {
        logger.Debug(
            "Unable to resolve device brand: got no value from WMI query for "
            "Win32_ComputerSystem.Manufacturer"
        );
      }
    } else {
      logger.Debug(
          "Unable to resolve device name, model, and brand: WMI COM initialization "
          "failed"
      );
    }

    // Get processor architecture
    SYSTEM_INFO sys_info;
    GetNativeSystemInfo(&sys_info);
    _device_info.architecture =
        MapProcessorArchitecture(sys_info.wProcessorArchitecture);
    if (_device_info.architecture.empty()) {
      logger.Debug(
          "Unable to resolve device architecture: GetNativeSystemInfo returned "
          "unrecognized architecture enum",
          {{"arch", static_cast<int64_t>(sys_info.wProcessorArchitecture)}}
      );
    }

    // Get user locale
    _device_info.locale = GetDeviceLocale(logger);

    // Get timezone (mapped to IANA timezone ID)
    _device_info.time_zone = GetDeviceTimezone(logger);
  }

  const OsInfo& GetOsInfo() const override { return _os_info; }
  const DeviceInfo& GetDeviceInfo() const override { return _device_info; }
};

std::unique_ptr<ISystemInfo> SystemInfo::Init(impl::DiagnosticLogger& logger) {
  return std::make_unique<WindowsSystemInfo>(logger);
}

}  // namespace datadog::platform
