// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/profiling.h"

#include "support/core.hpp"
#include "support/diagnostics.hpp"

namespace {

/** Returns true if the buffer holds a message in `messages` that contains `text`. */
bool HasMessage(
    const DiagnosticMessageBuffer& diagnostics,
    const std::vector<std::string>& messages,
    std::string_view text
) {
  std::lock_guard lock(diagnostics.mutex);
  return std::any_of(messages.begin(), messages.end(), [&](const std::string& message) {
    return message.find(text) != std::string::npos;
  });
}

}  // namespace

TEST_CASE("dd_profiling null safety", "[unit][profiling][c-api]") {
  SECTION("M safely do nothing W target object is null") {
    REQUIRE(dd_profiling_init(nullptr, nullptr) == nullptr);
    dd_profiling_destroy(nullptr);
    dd_profiling_config_init(nullptr);
  }
}

TEST_CASE("dd_profiling_config_init", "[unit][profiling][c-api]") {
  SECTION("M set the config version W initialized") {
    dd_profiling_config_t config = {};
    dd_profiling_config_init(&config);
    REQUIRE(config.version == 1);
  }
}

#ifndef WITH_DATADOG_PROFILING_TESTS

TEST_CASE("dd_profiling_init without profiling support", "[unit][profiling][c-api]") {
  SECTION("M log status and return null W the build has no profiler") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);

    // When we register the profiling feature
    dd_profiling_config_t config;
    dd_profiling_config_init(&config);
    dd_profiling_t* profiling = dd_profiling_init(core, &config);

    // Then we get no object, and a status message that explains why
    REQUIRE(profiling == nullptr);
    REQUIRE(HasMessage(
        test.diagnostics, test.diagnostics.status, "not supported in this build"
    ));

    dd_profiling_destroy(profiling);
    dd_core_destroy(core);
  }
}

#else

TEST_CASE("dd_profiling_init claims the profiler", "[unit][profiling][c-api]") {
  constexpr std::string_view ALREADY_ENABLED_WARNING = "already enabled";

  SECTION("M claim the profiler W no other core has") {
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);

    dd_profiling_t* profiling = dd_profiling_init(core, nullptr);

    REQUIRE(profiling != nullptr);
    REQUIRE_FALSE(
        HasMessage(test.diagnostics, test.diagnostics.warning, ALREADY_ENABLED_WARNING)
    );

    dd_profiling_destroy(profiling);
    dd_core_destroy(core);
  }

  SECTION("M refuse a second core W the first core still has the profiler") {
    auto first_test = CoreTestHarness::Init();
    dd_core_t* first_core = CoreTestHarness::WrapForC(first_test);
    dd_profiling_t* first_profiling = dd_profiling_init(first_core, nullptr);
    REQUIRE(first_profiling != nullptr);

    auto second_test = CoreTestHarness::Init();
    dd_core_t* second_core = CoreTestHarness::WrapForC(second_test);
    dd_profiling_t* second_profiling = dd_profiling_init(second_core, nullptr);

    REQUIRE(second_profiling == nullptr);
    REQUIRE(HasMessage(
        second_test.diagnostics,
        second_test.diagnostics.warning,
        ALREADY_ENABLED_WARNING
    ));

    dd_profiling_destroy(second_profiling);
    dd_core_destroy(second_core);
    dd_profiling_destroy(first_profiling);
    dd_core_destroy(first_core);
  }

  SECTION("M release the profiler W its core is destroyed but the handle is kept") {
    auto first_test = CoreTestHarness::Init();
    dd_core_t* first_core = CoreTestHarness::WrapForC(first_test);
    dd_profiling_t* first_profiling = dd_profiling_init(first_core, nullptr);
    REQUIRE(first_profiling != nullptr);

    dd_core_destroy(first_core);

    auto second_test = CoreTestHarness::Init();
    dd_core_t* second_core = CoreTestHarness::WrapForC(second_test);
    dd_profiling_t* second_profiling = dd_profiling_init(second_core, nullptr);
    REQUIRE_FALSE(HasMessage(
        second_test.diagnostics,
        second_test.diagnostics.warning,
        ALREADY_ENABLED_WARNING
    ));

    dd_profiling_destroy(second_profiling);
    dd_core_destroy(second_core);
    dd_profiling_destroy(first_profiling);
  }
}

#endif  // WITH_DATADOG_PROFILING_TESTS
