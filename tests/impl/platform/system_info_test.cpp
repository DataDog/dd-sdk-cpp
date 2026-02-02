// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/platform/system_info.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cctype>

#include "datadog/impl/diagnostics.hpp"

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

  // TODO(RUM-14016): Add tests for GetDeviceInfo() when implemented

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
#endif
}
