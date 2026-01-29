// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <comdef.h>
#include <wbemidl.h>
#include <windows.h>

#include <string>

#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/system_info.hpp"

#pragma comment(lib, "wbemuuid.lib")

namespace datadog::platform {

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

    // Enter a `do { ... } while (false)` loop for common cleanup pattern
    do {
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
        break;
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
        break;
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
        break;
      }

      initialized_ = true;
      return true;

    } while (false);

    return false;
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
    std::string result;
    IEnumWbemClassObject* enumerator = nullptr;

    // Enter a `do { ... } while (false)` loop for common cleanup pattern
    do {
      // Execute WMI query
      std::wstring query =
          L"SELECT " + std::wstring(property) + L" FROM " + std::wstring(wmi_class);
      hr = services_->ExecQuery(
          _bstr_t(L"WQL"),
          _bstr_t(query.c_str()),
          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
          nullptr,
          &enumerator
      );
      if (FAILED(hr)) {
        logger_.Debug(
            "Failed to execute WMI query", {{"hresult", static_cast<int64_t>(hr)}}
        );
        break;
      }

      // Get the first result
      IWbemClassObject* obj = nullptr;
      ULONG returned = 0;
      hr = enumerator->Next(WBEM_INFINITE, 1, &obj, &returned);
      if (FAILED(hr) || returned == 0) {
        if (obj) {
          obj->Release();
        }
        break;
      }

      // Get the property value
      VARIANT variant;
      VariantInit(&variant);
      hr = obj->Get(property, 0, &variant, nullptr, nullptr);
      if (SUCCEEDED(hr) && variant.vt == VT_BSTR && variant.bstrVal) {
        result = WideToUtf8(variant.bstrVal);
      }

      VariantClear(&variant);
      obj->Release();

    } while (false);

    // Clean up query-specific resources
    if (enumerator) {
      enumerator->Release();
    }

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
 * Retrieves the system timezone using GetDynamicTimeZoneInformation.
 *
 * For Phase 1, returns Windows timezone name as-is.
 * Phase 2 will add IANA mapping.
 *
 * @param logger Diagnostic logger for warnings
 * @return Timezone name (e.g., "Pacific Standard Time"), or empty on failure
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

  return WideToUtf8(tz_name);
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

    if (!GetWindowsVersion(version_info)) {
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

    // Get timezone (Phase 1: Windows name; Phase 2 will add IANA mapping)
    _device_info.time_zone = GetDeviceTimezone(logger);
  }

  const OsInfo& GetOsInfo() const override { return _os_info; }
  const DeviceInfo& GetDeviceInfo() const override { return _device_info; }
};

std::unique_ptr<ISystemInfo> SystemInfo::Init(impl::DiagnosticLogger& logger) {
  return std::make_unique<WindowsSystemInfo>(logger);
}

}  // namespace datadog::platform
