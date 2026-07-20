// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/platform/system_info.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cctype>
#include <limits>
#include <regex>

#include "datadog/impl/core/util/diagnostics.hpp"

using namespace datadog;

// [platform-system-info] tests validate the system info implementation.

TEST_CASE("SystemInfo", "[unit][platform-system-info]") {
  // Given a diagnostic logger (unused in successful cases)
  impl::DiagnosticLogger logger;

  SECTION("M return non-null instance W initialized") {
    // When we initialize the system info
    auto system_info = platform::SystemInfo::Init(logger);

    // Then it returns a valid instance
    REQUIRE(system_info != nullptr);
  }

  SECTION("M return valid PID W GetPid called") {
    // Given an initialized system info instance
    auto system_info = platform::SystemInfo::Init(logger);
    REQUIRE(system_info != nullptr);

    // When we retrieve the PID
    const int64_t pid = system_info->GetPid();

    // Then it is positive and within the valid int64_t range
    REQUIRE(pid > 0);
    REQUIRE(pid < std::numeric_limits<int64_t>::max());
  }

  SECTION("M return valid OS info W GetOsInfo called") {
    // Given an initialized system info instance
    auto system_info = platform::SystemInfo::Init(logger);
    REQUIRE(system_info != nullptr);

    // When we retrieve OS information
    const platform::OsInfo& os_info = system_info->GetOsInfo();

    // Then the OS name is non-empty
    REQUIRE(!os_info.name.empty());

    // And all string fields are reasonably sized (< 256 chars)
    REQUIRE(os_info.name.length() < 256);
    REQUIRE(os_info.version.length() < 256);
    REQUIRE(os_info.build.length() < 256);
    REQUIRE(os_info.version_major.length() < 256);

    // And version_major is a numeric string
    bool is_numeric = true;
    for (char c : os_info.version_major) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        is_numeric = false;
        break;
      }
    }
    REQUIRE(is_numeric);
  }

  SECTION("M return valid device info W GetDeviceInfo called") {
    // Given an initialized system info instance
    auto system_info = platform::SystemInfo::Init(logger);
    REQUIRE(system_info != nullptr);

    // When we retrieve device information
    const platform::DeviceInfo& device_info = system_info->GetDeviceInfo();

    // Then all string fields are reasonably sized (< 256 chars)
    REQUIRE(device_info.type.length() < 256);
    REQUIRE(device_info.name.length() < 256);
    REQUIRE(device_info.model.length() < 256);
    REQUIRE(device_info.brand.length() < 256);
    REQUIRE(device_info.architecture.length() < 256);
    REQUIRE(device_info.locale.length() < 256);
    REQUIRE(device_info.time_zone.length() < 256);
  }

// Platform-specific validation tests
#if defined(_WIN32)
  SECTION("M return Windows-specific OS info W running on Windows") {
    // Given an initialized system info instance on Windows
    auto system_info = platform::SystemInfo::Init(logger);
    REQUIRE(system_info != nullptr);

    // When we retrieve OS information
    const platform::OsInfo& os_info = system_info->GetOsInfo();

    // Then the OS name is "Windows"
    REQUIRE(os_info.name == "Windows");

    // And the version contains at least one dot (format: "major.minor.build")
    REQUIRE(os_info.version.find('.') != std::string::npos);

    // And version_major is numeric
    bool is_numeric = true;
    for (char c : os_info.version_major) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        is_numeric = false;
        break;
      }
    }
    REQUIRE(is_numeric);

    // And build is non-empty (should contain build number, possibly with UBR)
    REQUIRE(!os_info.build.empty());
  }

  SECTION("M return Windows-specific device info W running on Windows") {
    // Given an initialized system info instance on Windows
    auto system_info = platform::SystemInfo::Init(logger);
    REQUIRE(system_info != nullptr);

    // When we retrieve device information
    const platform::DeviceInfo& device_info = system_info->GetDeviceInfo();

    // Then the device type is "desktop"
    REQUIRE(device_info.type == "desktop");

    // And architecture is one of the expected values
    bool valid_arch = device_info.architecture == "x86" ||
                      device_info.architecture == "x86_64" ||
                      device_info.architecture == "arm64";
    REQUIRE(valid_arch);

    // And locale matches expected format (if present)
    if (!device_info.locale.empty()) {
      std::regex locale_regex("[a-z]{2}-[A-Z]{2}");
      REQUIRE(std::regex_match(device_info.locale, locale_regex));
    }
  }
#elif defined(__APPLE__)
  SECTION("M return macOS-specific OS info W running on macOS") {
    // Given an initialized system info instance on macOS
    auto system_info = platform::SystemInfo::Init(logger);
    REQUIRE(system_info != nullptr);

    // When we retrieve OS information
    const platform::OsInfo& os_info = system_info->GetOsInfo();

    // Then the OS name is "macOS"
    REQUIRE(os_info.name == "macOS");

    // And the build is non-empty (kern.osversion)
    REQUIRE(!os_info.build.empty());

    // And version is non-empty and not the default "0"
    REQUIRE(!os_info.version.empty());
    REQUIRE(os_info.version != "0");

    // And version_major is numeric
    bool is_numeric = true;
    for (char c : os_info.version_major) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        is_numeric = false;
        break;
      }
    }
    REQUIRE(is_numeric);
  }

  SECTION("M return macOS-specific device info W running on macOS") {
    // Given an initialized system info instance on macOS
    auto system_info = platform::SystemInfo::Init(logger);
    REQUIRE(system_info != nullptr);

    // When we retrieve device information
    const platform::DeviceInfo& device_info = system_info->GetDeviceInfo();

    // Then the device type is "desktop"
    REQUIRE(device_info.type == "desktop");

    // And the brand is "Apple"
    REQUIRE(device_info.brand == "Apple");

    // And the model starts with "Mac" or "VirtualMac" (if not empty); CI runs
    // in Apple Virtualization VMs, which report hw.model as
    // "VirtualMac<N>,<M>", while real hardware reports e.g. "MacBookPro18,3".
    if (!device_info.model.empty()) {
      bool valid_model_prefix = device_info.model.find("Mac") == 0 ||
                                device_info.model.find("VirtualMac") == 0;
      REQUIRE(valid_model_prefix);
    }

    // And the name is an alphabetical prefix of the model (if both are present)
    if (!device_info.name.empty() && !device_info.model.empty()) {
      REQUIRE(device_info.model.find(device_info.name) == 0);
    }

    // And architecture is either "x86_64" or "arm64"
    bool valid_arch =
        device_info.architecture == "x86_64" || device_info.architecture == "arm64";
    REQUIRE(valid_arch);
  }
#elif defined(__linux__)
  SECTION("M return Linux-specific OS info W running on Linux") {
    // Given an initialized system info instance on Linux
    auto system_info = platform::SystemInfo::Init(logger);
    REQUIRE(system_info != nullptr);

    // When we retrieve OS information
    const platform::OsInfo& os_info = system_info->GetOsInfo();

    // Then the OS name is non-empty
    REQUIRE(!os_info.name.empty());

    // And version_major is numeric
    bool is_numeric = true;
    for (char c : os_info.version_major) {
      if (!std::isdigit(static_cast<unsigned char>(c))) {
        is_numeric = false;
        break;
      }
    }
    REQUIRE(is_numeric);

    // And version is non-empty
    REQUIRE(!os_info.version.empty());
  }

  SECTION("M return Linux-specific device info W running on Linux") {
    // Given an initialized system info instance on Linux
    auto system_info = platform::SystemInfo::Init(logger);
    REQUIRE(system_info != nullptr);

    // When we retrieve device information
    const platform::DeviceInfo& device_info = system_info->GetDeviceInfo();

    // Then the device type is "desktop"
    REQUIRE(device_info.type == "desktop");

    // And architecture is non-empty (from uname)
    REQUIRE(!device_info.architecture.empty());
  }
#endif
}
