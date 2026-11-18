// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "datadog/crash_reporting.h"

// Registering Crash Reporting with a working core would install a process-global crash
// handler in the test process, so crash reporting behavior is covered by the
// integration tests instead. These tests cover only calls without a valid core.

TEST_CASE("dd_crash_reporting null safety", "[unit][crash_reporting][c-api]") {
  SECTION("M safely do nothing W the core is null") {
    // Given a config with a custom handler path, and a null config
    dd_crash_reporting_config_t* config = dd_crash_reporting_config_create();
    REQUIRE(config != nullptr);
    dd_crash_reporting_config_set_handler_exe_path(config, "crashpad_handler");
    std::vector<dd_crash_reporting_config_t*> configs = {config, nullptr};

    for (dd_crash_reporting_config_t* c : configs) {
      // When we register the Crash Reporting feature without a core
      dd_crash_reporting_t* crash_reporting = dd_crash_reporting_init(nullptr, c);

      // Then we get null, which is safe to destroy
      REQUIRE(crash_reporting == nullptr);
      dd_crash_reporting_destroy(crash_reporting);
    }
    dd_crash_reporting_config_destroy(config);
  }

  SECTION("M safely do nothing W the config is null") {
    dd_crash_reporting_config_set_handler_exe_path(nullptr, "crashpad_handler");
    dd_crash_reporting_config_destroy(nullptr);
  }
}
