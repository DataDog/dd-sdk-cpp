// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/crash_processing/crash_formatting.hpp"

#include <algorithm>
#include <csignal>
#include <regex>
#include <string>

#include "datadog/impl/core/feature_types/crash_reporting.hpp"

#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("FormatCrashReportErrorMessage", "[unit][rum]") {
  CrashReport crash{};

#ifdef _WIN32
  SECTION("M include structured exception code and name W exception type is known") {
    crash.fault_code = 0xc0000005;
    const auto message = FormatCrashReportErrorMessage(crash);
    REQUIRE(message == "Application crash: EXCEPTION_ACCESS_VIOLATION (0xC0000005)");
  }

  SECTION("M include raw code W exception type is unknown") {
    crash.fault_code = 0xdeadbeef;
    const auto message = FormatCrashReportErrorMessage(crash);
    REQUIRE(message == "Application crash: 0xDEADBEEF");
  }
#else
  SECTION("M include signal name and description W signal is known") {
    crash.fault_code = static_cast<uint64_t>(SIGSEGV);
    const auto message = FormatCrashReportErrorMessage(crash);
    REQUIRE(message == "Application crash: SIGSEGV (Segmentation fault)");
  }

  SECTION("M include raw signal number W signal is unknown") {
    crash.fault_code = 0xdeadbeef;
    const auto message = FormatCrashReportErrorMessage(crash);
    REQUIRE(message == "Application crash: signal 3735928559");
  }
#endif
}

TEST_CASE("FormatCrashReportStack", "[unit][rum]") {
  CrashReport crash{};

  SECTION("M return empty string W crash report has no stack") {
    const auto stack = FormatCrashReportStack(crash);
    REQUIRE(stack == "");
  }

  SECTION(
      "M use expected format W crash report has a stack with resolved and unresolved "
      "modules"
  ) {
    // Given a set of module information
    crash.modules.push_back({"MyApp", "abc123", "arm64", false, 0x1000, 0x5000});
    crash.modules.push_back(
        {"libsystem_c.dylib", "def456", "arm64", true, 0x100000, 0x200000}
    );

    // And a main-thread stack trace with multiple frames, some of which refer to those
    // modules and some of which are unresolved
    crash.stack.push_back({0x1388, 0, 0x388});    // resolved to MyApp
    crash.stack.push_back({0x1400, 0, 0x400});    // resolved to MyApp
    crash.stack.push_back({0xdeadbeef, -1, 0});   // unresolved
    crash.stack.push_back({0x100abc, 1, 0xabc});  // resolved to libsystem_c.dylib
    crash.stack.push_back({0xeee, 55, 55});       // erroneous out-of-range module_index

    // When we produce a formatted stack trace string from the crash report
    const auto stack = FormatCrashReportStack(crash);

    // Then it matches the expected format exactly
    const std::string want =
        "0   MyApp\t0x0000000000001388 0x1000 + 904\n"
        "1   MyApp\t0x0000000000001400 0x1000 + 1024\n"
        "2   ???\t0x00000000deadbeef 0x0 + 0\n"
        "3   libsystem_c.dylib\t0x0000000000100abc 0x100000 + 2748\n"
        "4   ???\t0x0000000000000eee 0x0 + 0\n";
    REQUIRE(stack == want);

    // And each line matches the regex used by parseNativeStack
    const std::regex pattern(
        R"(^([0-9]+)\s+(.+)\s+(0x[0-9a-f]{16}) (0x[0-9a-f]+) \+ ([0-9]+)$)"
    );
    size_t pos = 0;
    while (pos < stack.size()) {
      const size_t eol = stack.find('\n', pos);
      const size_t len = (eol == std::string::npos ? stack.size() : eol) - pos;
      if (len > 0) {
        const std::string line = stack.substr(pos, len);
        REQUIRE(std::regex_match(line, pattern));
      }
      if (eol == std::string::npos) {
        break;
      }
      pos = eol + 1;
    }
  }

  SECTION("M limit result to 512 frames W crash report stack exceeds 512 frames") {
    // Given a crash report with 1024 stack frames
    for (size_t i = 0; i < 1024; i++) {
      crash.stack.push_back({0xffffffffffffffff, -1, 0});
    }

    // When we produce a formatted stack trace string from the crash report
    const auto stack = FormatCrashReportStack(crash);

    // Then it only contains 512 total frames, and frame indices are appropriately
    // padded
    REQUIRE_THAT(stack, Catch::Matchers::ContainsSubstring("0   ???"));
    REQUIRE_THAT(stack, Catch::Matchers::ContainsSubstring("99  ???"));
    REQUIRE_THAT(stack, Catch::Matchers::ContainsSubstring("128 ???"));
    REQUIRE_THAT(stack, Catch::Matchers::ContainsSubstring("511 ???"));
    REQUIRE_THAT(stack, !Catch::Matchers::ContainsSubstring("512 ???"));
    REQUIRE(std::count(stack.begin(), stack.end(), '\n') == 512);
  }
}
