// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/profiling.hpp"

#include "support/core.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;

namespace {

// Warning that Profiling::Register logs when the build has no profiler
constexpr std::string_view NOT_SUPPORTED_WARNING = "not supported in this build";

/** Returns true if the buffer holds a warning that contains `text`. */
bool HasWarning(const DiagnosticMessageBuffer& diagnostics, std::string_view text) {
  std::lock_guard lock(diagnostics.mutex);
  return std::any_of(
      diagnostics.warning.begin(),
      diagnostics.warning.end(),
      [&](const std::string& message) {
        return message.find(text) != std::string::npos;
      }
  );
}

}  // namespace

TEST_CASE("Profiling::Register null safety", "[unit][profiling][cpp-api]") {
  SECTION("M return a no-op interface W the core is invalid") {
    // Given both a Core interface that has no valid implementation, and a null pointer
    CoreConfig invalid_config("", "", "");
    invalid_config.SetDiagnosticHandler(nullptr);
    std::shared_ptr<Core> noop_core =
        Core::Create(invalid_config, TrackingConsent::Granted);
    std::shared_ptr<Core> null_core;
    std::vector<std::shared_ptr<Core>> cores = {noop_core, null_core};

    for (const std::shared_ptr<Core>& core : cores) {
      // When we register the profiling feature on it
      auto profiling = Profiling::Register(core);

      // Then we get a valid object, whose destruction is harmless
      REQUIRE(profiling != nullptr);
    }
  }
}

#ifndef DD_TESTS_WITH_PROFILING

TEST_CASE(
    "Profiling::Register without profiling support", "[unit][profiling][cpp-api]"
) {
  SECTION("M warn and return a no-op interface W the build has no profiler") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // When we register the profiling feature
    auto profiling = Profiling::Register(core);

    // Then we get a valid object, and a warning that explains why it does nothing
    REQUIRE(profiling != nullptr);
    REQUIRE(HasWarning(test.diagnostics, NOT_SUPPORTED_WARNING));
  }
}

#else

namespace {

// Warning that Profiling::Register logs when another Core already has the profiler. It
// is only defined here, because only these tests use it.
constexpr std::string_view ALREADY_ENABLED_WARNING = "already enabled";

}  // namespace

TEST_CASE("Profiling::Register claims the profiler", "[unit][profiling][cpp-api]") {
  SECTION("M claim the profiler W no other core has") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // When we register the profiling feature
    auto profiling = Profiling::Register(core);

    // Then we get a valid object, with no warning
    REQUIRE(profiling != nullptr);
    REQUIRE_FALSE(HasWarning(test.diagnostics, NOT_SUPPORTED_WARNING));
    REQUIRE_FALSE(HasWarning(test.diagnostics, ALREADY_ENABLED_WARNING));
  }

  SECTION("M refuse a second core W the first core still has the profiler") {
    // Given a core that has registered the profiling feature
    auto first_test = CoreTestHarness::Init();
    auto first_core = CoreTestHarness::WrapForCpp(first_test);
    auto first_profiling = Profiling::Register(first_core);
    REQUIRE_FALSE(HasWarning(first_test.diagnostics, ALREADY_ENABLED_WARNING));

    // When a second core registers it
    auto second_test = CoreTestHarness::Init();
    auto second_core = CoreTestHarness::WrapForCpp(second_test);
    auto second_profiling = Profiling::Register(second_core);

    // Then it gets a no-op object, and a warning
    REQUIRE(second_profiling != nullptr);
    REQUIRE(HasWarning(second_test.diagnostics, ALREADY_ENABLED_WARNING));
  }

  SECTION("M release the profiler W its core is destroyed") {
    // Given a core that has registered the profiling feature
    auto first_test = CoreTestHarness::Init();
    auto first_core = CoreTestHarness::WrapForCpp(first_test);
    auto first_profiling = Profiling::Register(first_core);

    // When that core is destroyed
    first_profiling.reset();
    first_core.reset();

    // Then another core can register it
    auto second_test = CoreTestHarness::Init();
    auto second_core = CoreTestHarness::WrapForCpp(second_test);
    auto second_profiling = Profiling::Register(second_core);
    REQUIRE_FALSE(HasWarning(second_test.diagnostics, ALREADY_ENABLED_WARNING));
  }

  SECTION("M release the profiler W its core is destroyed but the handle is kept") {
    // Given a core that has registered the profiling feature, and an application that
    // keeps the Profiling handle it was given
    auto first_test = CoreTestHarness::Init();
    auto first_core = CoreTestHarness::WrapForCpp(first_test);
    auto first_profiling = Profiling::Register(first_core);

    // When that core is destroyed, but the handle is not
    first_core.reset();
    REQUIRE(first_profiling != nullptr);

    // Then another core can still register the profiler
    auto second_test = CoreTestHarness::Init();
    auto second_core = CoreTestHarness::WrapForCpp(second_test);
    auto second_profiling = Profiling::Register(second_core);
    REQUIRE_FALSE(HasWarning(second_test.diagnostics, ALREADY_ENABLED_WARNING));
  }
}

#endif  // DD_TESTS_WITH_PROFILING
