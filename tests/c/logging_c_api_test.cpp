// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <functional>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

#include "datadog/logging.h"

#include "datadog/impl/core/version.hpp"
#include "datadog/impl/logging/logger.hpp"
#include "datadog/impl/logging/logging.hpp"

#include "support/core.hpp"
#include "support/json_validation.hpp"
#include "support/threading.hpp"

using namespace datadog;

static const auto OS_PROPERTIES = nlohmann::json{
    {"name", "MockOS"}, {"version", "1.0.0"}, {"build", "12345"}, {"version_major", "1"}
};

static const auto DEVICE_PROPERTIES = nlohmann::json{
    {"architecture", "x86_64"},
    {"brand", "MockBrand"},
    {"locale", "en-US"},
    {"model", "MockModel"},
    {"name", "MockDevice"},
    {"time_zone", "UTC"},
    {"type", "desktop"}
};

static const nlohmann::json DDTAGS =
    "service:mock-service,version:mock-application-version,"
    "env:mock-env,sdk_version:1.2.3";

TEST_CASE("dd_logging null safety", "[unit][logging][c-api]") {
  SECTION("M safely do nothing W target object is null") {
    dd_attribute_t int_100 = dd_attribute_int(100);
    dd_attribute_t obj = dd_attribute_object(0);

    REQUIRE(dd_logging_init(nullptr) == nullptr);
    dd_logging_destroy(nullptr);
    dd_logging_add_attribute(nullptr, "foo", &int_100);
    dd_logging_remove_attribute(nullptr, "foo");

    dd_logger_config_init(nullptr);
    dd_logger_config_set_remote_sample_rate(nullptr, 0.5f);
    dd_logger_config_set_service(nullptr, "service");
    dd_logger_config_set_name(nullptr, "logger");
    dd_logger_config_set_remote_log_threshold(nullptr, DD_LOG_LEVEL_INFO);
    dd_logger_config_set_initial_attribute_capacity(nullptr, 8);
    dd_logger_config_set_enrich_with_rum_context(nullptr, false);

    REQUIRE(dd_logger_create(nullptr, nullptr) == nullptr);
    dd_logger_destroy(nullptr);
    dd_logger_add_attribute(nullptr, "foo", &int_100);
    dd_logger_remove_attribute(nullptr, "foo");
    dd_logger_add_tag(nullptr, "foo:bar");
    dd_logger_add_tag_kv(nullptr, "foo", "bar");
    dd_logger_remove_tag(nullptr, "foo:bar");
    dd_logger_remove_tags_with_key(nullptr, "foo");
    dd_logger_log(nullptr, DD_LOG_LEVEL_INFO, "hello", nullptr, &obj);
    dd_logger_debug(nullptr, "hello", &obj);
    dd_logger_info(nullptr, "hello", &obj);
    dd_logger_notice(nullptr, "hello", &obj);
    dd_logger_warn(nullptr, "hello", &obj);
    dd_logger_error(nullptr, "hello", nullptr, &obj);
    dd_logger_critical(nullptr, "hello", nullptr, &obj);

    dd_attribute_free(&obj);
    dd_attribute_free(&int_100);
  }
}

TEST_CASE("dd_logging_init", "[unit][logging][c-api]") {
  SECTION("M return valid feature W initialized with valid core") {
    // Given a valid dd_core_t
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);

    // When we call dd_logging_init()
    dd_logging_t* logging = dd_logging_init(core);

    // Then we get a valid dd_logging_t instance
    REQUIRE(logging);

    // Cleanup
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M return null W feature registered multiple times") {
    // Given a valid dd_core_t that already has logging enabled
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    REQUIRE(logging);

    // When we attempt to call dd_logging_init a second time
    dd_logging_t* other_logging = dd_logging_init(core);

    // Then we get null
    REQUIRE(other_logging == nullptr);

    // Cleanup
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }
}

TEST_CASE("dd_logger_create", "[unit][logging][c-api]") {
  SECTION("M return valid logger W initialized with valid logging feature") {
    // Given a valid dd_core_t and dd_logging_t
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);

    // And a core that's already been started
    REQUIRE(dd_core_start(core));

    // When we call dd_logger_create
    dd_logger_config_t logger_config;
    dd_logger_config_init(&logger_config);
    dd_logger_config_set_service(&logger_config, "my-cool-service");
    dd_logger_config_set_name(&logger_config, "my-cool-logger");
    dd_logger_t* logger = dd_logger_create(logging, &logger_config);

    // Then we get a valid logger
    REQUIRE(logger);

    // And we get no diagnostic errors or warnings
    REQUIRE(test.diagnostics.warning.size() == 0);
    REQUIRE(test.diagnostics.error.size() == 0);

    // Cleanup
    dd_core_stop(core);
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M return valid logger W core not yet started") {
    // Given a valid dd_core_t and dd_logging_t
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);

    // When we call dd_logger_create
    dd_logger_config_t logger_config;
    dd_logger_config_init(&logger_config);
    dd_logger_config_set_service(&logger_config, "my-cool-service");
    dd_logger_config_set_name(&logger_config, "my-cool-logger");
    dd_logger_t* logger = dd_logger_create(logging, &logger_config);

    // Then we get a valid logger, even though the SDK isn't running yet
    REQUIRE(logger);
    REQUIRE(test.diagnostics.warning.size() == 0);
    REQUIRE(test.diagnostics.error.size() == 0);

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M use default config W provided config is null") {
    // Given a valid dd_core_t and dd_logging_t
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);

    // When we call dd_logger_create, passing NULL for our config argument
    dd_logger_t* logger = dd_logger_create(logging, nullptr);

    // Then we get a valid logger
    REQUIRE(logger);

    // And we get no diagnostic errors or warnings
    REQUIRE(test.diagnostics.warning.size() == 0);
    REQUIRE(test.diagnostics.error.size() == 0);

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }
}

TEST_CASE("dd_logging argument validation", "[unit][logging][c-api]") {
  // Given a series of tests consisting of a set of API calls and the warnings and/or
  // errors we expect to get in response
  struct TestParams {
    std::string_view name;
    std::function<void(dd_logging_t*)> func;
    std::vector<std::string> want_warnings;
    std::vector<std::string> want_errors;
  };
  std::vector<TestParams> tests = {

      // === Basic usage with no errors/warnings expected ===

      {"M print no warnings or errors W used normally",
       [](dd_logging_t* logging) {
         // Add and remove global attributes
         dd_attribute_t int_val = dd_attribute_int(100);
         dd_logging_add_attribute(logging, "foo", &int_val);
         dd_attribute_set_int(&int_val, 200);
         dd_logging_add_attribute(logging, "bar", &int_val);
         dd_logging_remove_attribute(logging, "foo");

         // Create a logger with the default config
         dd_logger_t* logger_a = dd_logger_create(logging, nullptr);

         // Create a logger with a custom config
         dd_logger_config_t config;
         dd_logger_config_init(&config);
         dd_logger_config_set_remote_sample_rate(&config, 100.0f);
         dd_logger_config_set_service(&config, "my-service");
         dd_logger_config_set_name(&config, "my-logger");
         dd_logger_config_set_remote_log_threshold(&config, DD_LOG_LEVEL_DEBUG);
         dd_logger_config_set_initial_attribute_capacity(&config, 8);
         dd_logger_t* logger_b = dd_logger_create(logging, &config);

         // Add and remove logger attributes
         dd_attribute_t str_val = dd_attribute_string("hello");
         dd_logger_add_attribute(logger_a, "bar", &str_val);
         dd_attribute_set_string(&str_val, "world");
         dd_logger_add_attribute(logger_a, "baz", &str_val);
         dd_logger_remove_attribute(logger_a, "bar");

         // Log a message
         dd_logger_warn(logger_a, "hello", nullptr);

         // Log a message with attributes
         dd_attribute_t message_attributes = dd_attribute_object(1);
         dd_attribute_object_property_set(&message_attributes, "bar", &str_val);
         dd_logger_info(logger_b, "goodbye", &message_attributes);
         dd_attribute_free(&message_attributes);

         // Cleanup
         dd_attribute_free(&int_val);
         dd_attribute_free(&str_val);
         dd_logger_destroy(logger_a);
         dd_logger_destroy(logger_b);
       },
       // All of the above should complete with 0 warnings and 0 errors
       {},
       {}},

      // === dd_logging_add_attribute() ===

      {"M print warning W dd_logging_add_attribute is called with NULL name",
       [](dd_logging_t* logging) {
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_logging_add_attribute(logging, nullptr, &int_100);
         dd_attribute_free(&int_100);
       },
       {"dd_logging_add_attribute call ignored: application must supply an attribute "
        "name"},
       {}},

      {"M print warning W dd_logging_add_attribute is called with NULL value",
       [](dd_logging_t* logging) { dd_logging_add_attribute(logging, "foo", nullptr); },
       {"dd_logging_add_attribute call ignored: application must supply an attribute "
        "value"},
       {}},

      // === dd_logging_remove_attribute() ===

      {"M print warning W dd_logging_remove_attribute is called with NULL name",
       [](dd_logging_t* logging) { dd_logging_remove_attribute(logging, nullptr); },
       {"dd_logging_remove_attribute call ignored: application must supply an "
        "attribute name"},
       {}},

      // === dd_logger_create() ===

      {"M print warning W dd_logger_config_t not properly initialized",
       [](dd_logging_t* logging) {
         dd_logger_config_t config;
         std::memset(&config, 0, sizeof(config));
         dd_logger_t* logger = dd_logger_create(logging, &config);
         REQUIRE(logger);
         dd_logger_destroy(logger);
       },
       {"dd_logger_create falling back to default config: application must initialize "
        "dd_logger_config_t value via dd_logger_config_init"},
       {}},

      // === dd_logger_add_attribute() ===

      {"M print warning W dd_logger_add_attribute is called with NULL name",
       [](dd_logging_t* logging) {
         dd_logger_t* logger = dd_logger_create(logging, nullptr);
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_logger_add_attribute(logger, nullptr, &int_100);
         dd_attribute_free(&int_100);
         dd_logger_destroy(logger);
       },
       {"dd_logger_add_attribute call ignored: application must supply an attribute "
        "name"},
       {}},

      {"M print warning W dd_logger_add_attribute is called with NULL value",
       [](dd_logging_t* logging) {
         dd_logger_t* logger = dd_logger_create(logging, nullptr);
         dd_logger_add_attribute(logger, "foo", nullptr);
         dd_logger_destroy(logger);
       },
       {"dd_logger_add_attribute call ignored: application must supply an attribute "
        "value"},
       {}},

      // === dd_logger_remove_attribute() ===

      {"M print warning W dd_logger_remove_attribute is called with NULL name",
       [](dd_logging_t* logging) {
         dd_logger_t* logger = dd_logger_create(logging, nullptr);
         dd_logger_remove_attribute(logger, nullptr);
         dd_logger_destroy(logger);
       },
       {"dd_logger_remove_attribute call ignored: application must supply an attribute "
        "name"},
       {}},

      // === dd_logger_add_tag() / dd_logger_add_tag_kv() ===

      {"M reject tag with error W dd_logger_add_tag is called with NULL tag",
       [](dd_logging_t* logging) {
         dd_logger_t* logger = dd_logger_create(logging, nullptr);
         dd_logger_add_tag(logger, nullptr);
         dd_logger_destroy(logger);
       },
       {},
       {"Logger tag was rejected: tags must not be empty {\"key\":\"\"}"}},

      {"M reject tag with error W dd_logger_add_tag_kv is called with NULL key",
       [](dd_logging_t* logging) {
         dd_logger_t* logger = dd_logger_create(logging, nullptr);
         dd_logger_add_tag_kv(logger, nullptr, "value");
         dd_logger_destroy(logger);
       },
       {},
       {"Logger tag was rejected: tags must not be empty {\"key\":\"\"}"}},
  };
  for (const auto& tt : tests) {
    DYNAMIC_SECTION(tt.name) {
      // Given a started core with the logging feature registered
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      dd_core_t* core = CoreTestHarness::WrapForC(test);
      dd_logging_t* logging = dd_logging_init(core);
      REQUIRE(dd_core_start(core));

      // When we execute our test function to exercise the logging API
      tt.func(logging);
      dd_core_stop(core);

      // Then we get the expected set of diagnostic warnings and errors
      REQUIRE(test.diagnostics.warning == tt.want_warnings);
      REQUIRE(test.diagnostics.error == tt.want_errors);
    }
  }
}

TEST_CASE("dd_logger_log", "[unit][logging][c-api]") {
  SECTION("M a emit log message W log is called") {
    // Given a valid logger and a started core
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, nullptr);
    dd_core_start(core);

    // When we emit a bunch of log messages and stop the core
    dd_logger_log(logger, DD_LOG_LEVEL_INFO, "hello", nullptr, nullptr);
    dd_logger_debug(logger, "gubed", nullptr);
    dd_logger_info(logger, "ofni", nullptr);
    dd_logger_notice(logger, "eciton", nullptr);
    dd_logger_warn(logger, "nraw", nullptr);
    dd_logger_error(logger, "rorre", nullptr, nullptr);
    dd_logger_critical(logger, "lacitirc", nullptr, nullptr);
    dd_core_stop(core);

    // Then the SDK sends a request that contains our log messages
    REQUIRE(test.client.requests.size() == 1);
    const MockHttpRequest& req = test.client.requests.front();
    REQUIRE(
        req.url ==
        "https://browser-intake-datadoghq.com/api/v2/logs?ddsource=mock-source"
    );
    REQUIRE(req.headers.find("Content-Type: application/json") != std::string::npos);
    REQUIRE(
        MergeJsonArrays(test.client.requests) ==
        nlohmann::json{
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "info"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "hello"},
                {"logger.version", "1.2.3"},
                {"ddtags", DDTAGS}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "debug"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "gubed"},
                {"logger.version", "1.2.3"},
                {"ddtags", DDTAGS}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "info"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "ofni"},
                {"logger.version", "1.2.3"},
                {"ddtags", DDTAGS}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "notice"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "eciton"},
                {"logger.version", "1.2.3"},
                {"ddtags", DDTAGS}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "warn"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "nraw"},
                {"logger.version", "1.2.3"},
                {"ddtags", DDTAGS}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "error"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "rorre"},
                {"logger.version", "1.2.3"},
                {"ddtags", DDTAGS}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "critical"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "lacitirc"},
                {"logger.version", "1.2.3"},
                {"ddtags", DDTAGS}
            },
        }
    );

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M do nothing W log is called before start") {
    // Given a valid logger and a core that's not yet started
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, nullptr);

    // When we emit a log message prior to starting the core
    dd_logger_info(logger, "ofni", nullptr);

    // And we start and stop the core thereafter
    dd_core_start(core);
    dd_core_stop(core);

    // Then no events are generated and no requests are sent, as any API calls
    // made before core start are simply no-ops
    REQUIRE(test.fs.Ls("app/.datadog/main/12345/logging/intermediate-v1").size() == 0);
    REQUIRE(test.fs.Ls("app/.datadog/main/12345/logging/v1").size() == 0);
    REQUIRE(test.client.requests.size() == 0);

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M do nothing W log is called after shutdown") {
    // Given a valid logger and a started core
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, nullptr);
    dd_core_start(core);

    // When we stop the core
    dd_core_stop(core);

    // And make a log call thereafter
    dd_logger_info(logger, "hello", nullptr);

    // Then no events are generated and no requests are sent, as any API calls
    // made after core stop are simply no-ops
    REQUIRE(test.fs.Ls("app/.datadog/main/12345/logging/intermediate-v1").size() == 0);
    REQUIRE(test.fs.Ls("app/.datadog/main/12345/logging/v1").size() == 0);
    REQUIRE(test.client.requests.size() == 0);

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M do nothing W log is called on orphaned logger") {
    // Given a valid logger and a started core
    auto test = CoreTestHarness::Init();
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, nullptr);
    dd_core_start(core);

    // When we stop the core, destroy the logging feature, and destroy the core,
    // without destroying the logger
    dd_core_stop(core);
    dd_logging_destroy(logging);
    dd_core_destroy(core);

    // And we make a log call thereafter
    dd_logger_info(logger, "hello", nullptr);

    // Then this is still a safe no-op, as the logger communicates the event to the
    // logging feature via a weak_ptr

    // Cleanup
    dd_logger_destroy(logger);
  }

  SECTION("M continue normally W logging feature is destroyed prior to core stop") {
    // Given a started core with a logger
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, nullptr);
    dd_core_start(core);

    // When we destroy the dd_logging_t
    dd_logging_destroy(logging);

    // And we emit a log message and stop the core
    dd_logger_info(logger, "hello", nullptr);
    dd_core_stop(core);

    // Then this is safe, as the API's references to features are shared: the API
    // can no longer access the feature, but the Core's shared_ptr keeps it alive
    REQUIRE(test.client.requests.size() == 1);
    REQUIRE(
        MergeJsonArrays(test.client.requests) ==
        nlohmann::json::array({nlohmann::json{
            {"os", OS_PROPERTIES},
            {"device", DEVICE_PROPERTIES},
            {"status", "info"},
            {"service", "mock-service"},
            {"date", "2023-11-14T22:13:20.000Z"},
            {"message", "hello"},
            {"logger.version", "1.2.3"},
            {"ddtags", DDTAGS}
        }})
    );

    // Cleanup
    dd_logger_destroy(logger);
    dd_core_destroy(core);
  }

  SECTION("M use name and service W provided via logger config") {
    // Given a logger config that sets 'name' and 'service'
    dd_logger_config_t config;
    dd_logger_config_init(&config);
    dd_logger_config_set_service(&config, "overridden-service");
    dd_logger_config_set_name(&config, "my-logger");

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, &config);
    dd_core_start(core);

    // When we emit a log message and stop the core
    dd_logger_info(logger, "hello", nullptr);
    dd_core_stop(core);

    // Then the resulting log event contains our logger-specific 'name' and
    // 'event'
    REQUIRE(test.client.requests.size() == 1);
    REQUIRE(
        MergeJsonArrays(test.client.requests) ==
        nlohmann::json::array({nlohmann::json{
            {"os", OS_PROPERTIES},
            {"device", DEVICE_PROPERTIES},
            {"status", "info"},
            {"service", "overridden-service"},
            {"date", "2023-11-14T22:13:20.000Z"},
            {"message", "hello"},
            {"logger.name", "my-logger"},
            {"logger.version", "1.2.3"},
            {"ddtags",
             "service:overridden-service,version:mock-application-version,"
             "env:mock-env,sdk_version:1.2.3"}
        }})
    );

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M use default name and service W explicitly set to empty string or NULL") {
    // Given either empty string or NULL as an input value
    std::array<const char*, 2> values = {"", nullptr};
    for (const char* value : values) {
      // When we set both 'name' and 'service' on a logger config to a valid string,
      // then set our null/empty value
      dd_logger_config_t config;
      dd_logger_config_init(&config);
      dd_logger_config_set_service(&config, "unused-service-name");
      dd_logger_config_set_name(&config, "unused-logger-name");
      dd_logger_config_set_service(&config, value);
      dd_logger_config_set_name(&config, value);

      // And we start a core running a logger with that config
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      dd_core_t* core = CoreTestHarness::WrapForC(test);
      dd_logging_t* logging = dd_logging_init(core);
      dd_logger_t* logger = dd_logger_create(logging, &config);
      dd_core_start(core);

      // And we emit a log message and stop the core
      dd_logger_info(logger, "hello", nullptr);
      dd_core_stop(core);

      // Then the resulting log event contains our default service name and no explicit
      // logger name, just as if we hadn't set any service or logger name
      REQUIRE(test.client.requests.size() == 1);
      REQUIRE(
          MergeJsonArrays(test.client.requests) ==
          nlohmann::json::array({nlohmann::json{
              {"os", OS_PROPERTIES},
              {"device", DEVICE_PROPERTIES},
              {"status", "info"},
              {"service", "mock-service"},
              {"date", "2023-11-14T22:13:20.000Z"},
              {"message", "hello"},
              {"logger.version", "1.2.3"},
              {"ddtags", DDTAGS}
          }})
      );

      // Cleanup
      dd_logger_destroy(logger);
      dd_logging_destroy(logging);
      dd_core_destroy(core);
    }
  }

  SECTION("M truncate name and service W provided values exceed supported lengths") {
    // Given an input string of length 208 (8 * 26)
    static const char* long_string =
        "aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhhiiiiiiiijjjjjj"
        "jjkkkkkkkkllllllllmmmmmmmmnnnnnnnnooooooooppppppppqqqqqqqqrrrrrrrrsssssssstttt"
        "ttttuuuuuuuuvvvvvvvvwwwwwwwwxxxxxxxxyyyyyyyyzzzzzzzz";

    // And a logger config that sets both 'name' and 'service' to that very long value
    dd_logger_config_t config;
    dd_logger_config_init(&config);
    dd_logger_config_set_service(&config, long_string);
    dd_logger_config_set_name(&config, long_string);

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, &config);
    dd_core_start(core);

    // When we emit a log message and stop the core
    dd_logger_info(logger, "hello", nullptr);
    dd_core_stop(core);

    // Then the 'name' and 'service' values reflected in the resulting log event are
    // exactly 63 characters and 127 characters, respectively, as the input values were
    // silently truncated at the character limit
    REQUIRE(test.client.requests.size() == 1);
    REQUIRE(
        MergeJsonArrays(test.client.requests) ==
        nlohmann::json::array({nlohmann::json{
            {"os", OS_PROPERTIES},
            {"device", DEVICE_PROPERTIES},
            {"status", "info"},
            {"service",
             "aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhhiiiiiiiij"
             "jjjjjjjkkkkkkkkllllllllmmmmmmmmnnnnnnnnooooooooppppppp"},
            {"date", "2023-11-14T22:13:20.000Z"},
            {"message", "hello"},
            {"logger.name",
             "aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhh"},
            {"logger.version", "1.2.3"},
            {"ddtags",
             "service:aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggg"
             "hhhhhhhhiiiiiiiijjjjjjjjkkkkkkkkllllllllmmmmmmmmnnnnnnnnooooooooppppppp,"
             "version:mock-application-version,env:mock-env,sdk_version:1.2.3"}
        }})
    );

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M emit only messages at or above threshold W log threshold is set") {
    // Given a logger config that sets a remote log threshold at 'error'
    dd_logger_config_t config;
    dd_logger_config_init(&config);
    dd_logger_config_set_remote_log_threshold(&config, DD_LOG_LEVEL_ERROR);

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, &config);
    dd_core_start(core);

    // When we emit 10 warning messages, 10 error messages, and 10 critical
    // messages, then stop the core
    for (int i = 0; i < 10; i++) {
      dd_logger_warn(logger, "perhaps this is bad", nullptr);
      dd_logger_error(logger, "this is definitely bad", nullptr, nullptr);
      dd_logger_critical(logger, "hold the phone, this is very bad", nullptr, nullptr);
    }
    dd_core_stop(core);

    // Then we should have sent exactly 20 events
    REQUIRE(test.client.requests.size() > 0);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() == 20);

    // And those events should consist of 10 errors and 10 critical errors
    auto has_status = [](const std::string_view status, const nlohmann::json& obj) {
      REQUIRE(obj.is_object());
      REQUIRE(obj.at("status").is_string());
      return obj.at("status").get<std::string>() == status;
    };
    auto is_warn = [&](const nlohmann::json& obj) { return has_status("warn", obj); };
    auto is_err = [&](const nlohmann::json& obj) { return has_status("error", obj); };
    auto is_crit = [&](const nlohmann::json& obj) {
      return has_status("critical", obj);
    };
    const size_t num_warn = std::count_if(events.begin(), events.end(), is_warn);
    const size_t num_err = std::count_if(events.begin(), events.end(), is_err);
    const size_t num_crit = std::count_if(events.begin(), events.end(), is_crit);
    REQUIRE(num_warn == 0);
    REQUIRE(num_err == 10);
    REQUIRE(num_crit == 10);
  }

  SECTION("M emit only a subset of messages W sampling rate is <1") {
    // Given a logger config that sets a remote sampling rate of 50%
    dd_logger_config_t config;
    dd_logger_config_init(&config);
    dd_logger_config_set_remote_sample_rate(&config, 50.0f);

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, &config);
    dd_core_start(core);

    // When we emit 1000 log messages and stop the core
    for (int i = 0; i < 1000; i++) {
      dd_logger_info(logger, "hello", nullptr);
    }
    dd_core_stop(core);

    // Then we should have sent ~500 log messages, +/- 100
    REQUIRE(test.client.requests.size() > 0);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() >= 400);
    REQUIRE(events.size() <= 600);

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M emit no messages W sampling rate is 0") {
    // Given a logger config that sets a remote sampling rate of 0%
    dd_logger_config_t config;
    dd_logger_config_init(&config);
    dd_logger_config_set_remote_sample_rate(&config, 0.0f);

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, &config);
    dd_core_start(core);

    // When we emit 1000 log messages and stop the core
    for (int i = 0; i < 1000; i++) {
      dd_logger_info(logger, "hello", nullptr);
    }
    dd_core_stop(core);

    // Then we should have sent no log messages whatsoever
    REQUIRE(test.client.requests.size() == 0);

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION("M apply sampling rate after log threshold W both are set") {
    // Given a logger config that sets a remote sampling rate of 50% and a remote
    // log threshold at 'info' and above
    dd_logger_config_t config;
    dd_logger_config_init(&config);
    dd_logger_config_set_remote_sample_rate(&config, 50.0f);
    dd_logger_config_set_remote_log_threshold(&config, DD_LOG_LEVEL_INFO);

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, &config);
    dd_core_start(core);

    // When we emit 500 debug message and 500 log messages, then stop the core
    for (int i = 0; i < 500; i++) {
      dd_logger_debug(logger, "this is not very important", nullptr);
      dd_logger_info(logger, "this might be important", nullptr);
    }
    dd_core_stop(core);

    // Then we should have sent ~250 log messages, +/- 50
    REQUIRE(test.client.requests.size() > 0);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() >= 200);
    REQUIRE(events.size() <= 300);

    // And they all should be 'info' messages
    const size_t num_info =
        std::count_if(events.begin(), events.end(), [](const nlohmann::json& obj) {
          REQUIRE(obj.is_object());
          REQUIRE(obj.at("status").is_string());
          return obj.at("status") == "info";
        });
    REQUIRE(num_info == events.size());

    // Cleanup
    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }
}

TEST_CASE("dd_logger attributes", "[unit][logging][c-api]") {
  // These tests register the logging feature and initialize a logger, then they
  // exercise the logger while the core is running and examine the resulting HTTP
  // request to see if it matches want_request_body
  struct TestParams {
    std::string_view name;
    std::function<void(dd_logging_t*)> pre_logger_init;
    std::function<void(dd_logging_t*, dd_logger_t*)> post_logger_init;
    std::function<void(dd_logging_t*, dd_logger_t*)> while_running;
    nlohmann::json want_request_body;
  };
  std::vector<TestParams> tests = {
      {"M include custom attribute W set globally on logging feature",
       [](dd_logging_t* logging) {
         // Set "foo":100 globally before logger is created
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_logging_add_attribute(logging, "foo", &int_100);
         dd_attribute_free(&int_100);
       },
       [](dd_logging_t* logging, dd_logger_t*) {
         // Set "bar":"yes" globally after logger is created
         dd_attribute_t string_yes = dd_attribute_string("yes");
         dd_logging_add_attribute(logging, "bar", &string_yes);
         dd_attribute_free(&string_yes);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Emit a message from our logger
         dd_logger_info(logger, "hello", nullptr);
       },
       // Event should include "foo":100,"bar":"yes"
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", "1.2.3"},
           {"ddtags", DDTAGS},
           {"foo", 100},
           {"bar", "yes"}
       }})},

      {"M include custom attribute W set on logger",
       [](dd_logging_t* logging) {
         // Set "foo":100 globally
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_logging_add_attribute(logging, "foo", &int_100);
         dd_attribute_free(&int_100);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Set "bar":200 on logger
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_logger_add_attribute(logger, "bar", &int_200);
         dd_attribute_free(&int_200);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Emit a message from our logger
         dd_logger_info(logger, "hello", nullptr);
       },
       // Event should include "foo":100,"bar":200
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", "1.2.3"},
           {"ddtags", DDTAGS},
           {"foo", 100},
           {"bar", 200}
       }})},

      {"M override global property W logger property has same name",
       [](dd_logging_t* logging) {
         // Set "foo":100 globally
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_logging_add_attribute(logging, "foo", &int_100);
         dd_attribute_free(&int_100);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Set "foo":200 on logger
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_logger_add_attribute(logger, "foo", &int_200);
         dd_attribute_free(&int_200);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Emit a message from our logger
         dd_logger_info(logger, "hello", nullptr);
       },
       // Event should include "foo":200
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", "1.2.3"},
           {"ddtags", DDTAGS},
           {"foo", 200}
       }})},

      {"M include custom attribute W set on message",
       [](dd_logging_t* logging) {
         // Set "foo":100 globally
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_logging_add_attribute(logging, "foo", &int_100);
         dd_attribute_free(&int_100);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Set "bar":200 on logger
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_logger_add_attribute(logger, "bar", &int_200);
         dd_attribute_free(&int_200);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Set "baz":300 on an object attribute that will be passed with log calls
         dd_attribute_t obj = dd_attribute_object(1);
         dd_attribute_t int_300 = dd_attribute_int(300);
         dd_attribute_object_property_set(&obj, "baz", &int_300);
         dd_attribute_free(&int_300);

         // Emit messages from our logger, testing all functions for coverage
         dd_logger_log(logger, DD_LOG_LEVEL_INFO, "hello", nullptr, &obj);
         dd_logger_debug(logger, "gubed", &obj);
         dd_logger_info(logger, "ofni", &obj);
         dd_logger_notice(logger, "eciton", &obj);
         dd_logger_warn(logger, "nraw", &obj);
         dd_logger_error(logger, "rorre", nullptr, &obj);
         dd_logger_critical(logger, "lacitirc", nullptr, &obj);

         dd_attribute_free(&obj);
       },
       // Event should include "foo":100,"bar":200,"baz":300
       nlohmann::json{
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "info"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "hello"},
               {"logger.version", "1.2.3"},
               {"ddtags", DDTAGS},
               {"foo", 100},
               {"bar", 200},
               {"baz", 300}
           },
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "debug"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "gubed"},
               {"logger.version", "1.2.3"},
               {"ddtags", DDTAGS},
               {"foo", 100},
               {"bar", 200},
               {"baz", 300}
           },
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "info"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "ofni"},
               {"logger.version", "1.2.3"},
               {"ddtags", DDTAGS},
               {"foo", 100},
               {"bar", 200},
               {"baz", 300}
           },
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "notice"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "eciton"},
               {"logger.version", "1.2.3"},
               {"ddtags", DDTAGS},
               {"foo", 100},
               {"bar", 200},
               {"baz", 300}
           },
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "warn"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "nraw"},
               {"logger.version", "1.2.3"},
               {"ddtags", DDTAGS},
               {"foo", 100},
               {"bar", 200},
               {"baz", 300}
           },
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "error"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "rorre"},
               {"logger.version", "1.2.3"},
               {"ddtags", DDTAGS},
               {"foo", 100},
               {"bar", 200},
               {"baz", 300}
           },
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "critical"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "lacitirc"},
               {"logger.version", "1.2.3"},
               {"ddtags", DDTAGS},
               {"foo", 100},
               {"bar", 200},
               {"baz", 300}
           }
       }},

      {"M override global and logger properties W message property has same name",
       [](dd_logging_t* logging) {
         // Set "foo":100,"baz":100 globally
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_logging_add_attribute(logging, "foo", &int_100);
         dd_logging_add_attribute(logging, "baz", &int_100);
         dd_attribute_free(&int_100);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Set "bar":200,"baz":200 on logger
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_logger_add_attribute(logger, "bar", &int_200);
         dd_logger_add_attribute(logger, "baz", &int_200);
         dd_attribute_free(&int_200);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Log a message with "foo":300,"bar":400
         dd_attribute_t obj = dd_attribute_object(2);
         dd_attribute_t int_300 = dd_attribute_int(300);
         dd_attribute_t int_400 = dd_attribute_int(400);
         dd_attribute_object_property_set(&obj, "foo", &int_300);
         dd_attribute_object_property_set(&obj, "bar", &int_400);
         dd_attribute_free(&int_300);
         dd_attribute_free(&int_400);

         dd_logger_info(logger, "hello", &obj);
         dd_attribute_free(&obj);
       },
       // Event should include "foo":300,"baz":200,"bar":400
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", "1.2.3"},
           {"ddtags", DDTAGS},
           {"foo", 300},
           {"baz", 200},
           {"bar", 400}
       }})},

      {"M not override attributes W property name is reserved",
       [](dd_logging_t* logging) {
         // Set "status":100,"ok-global":100 globally
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_logging_add_attribute(logging, "status", &int_100);
         dd_logging_add_attribute(logging, "ok-global", &int_100);
         dd_attribute_free(&int_100);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Set "service":200,"ok-logger":200 on logger
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_logger_add_attribute(logger, "service", &int_200);
         dd_logger_add_attribute(logger, "ok-logger", &int_200);
         dd_attribute_free(&int_200);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Log a message with "message":300,"_dd":300,"ok-message":300
         dd_attribute_t obj = dd_attribute_object(3);
         dd_attribute_t int_300 = dd_attribute_int(300);
         dd_attribute_object_property_set(&obj, "message", &int_300);
         dd_attribute_object_property_set(&obj, "_dd", &int_300);
         dd_attribute_object_property_set(&obj, "ok-message", &int_300);
         dd_attribute_free(&int_300);

         dd_logger_info(logger, "hello", &obj);
         dd_attribute_free(&obj);
       },
       // Event should include "ok-global":100,"ok-logger":200,"ok-message":300, but
       // all other custom attributes should be entirely ignored
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", "1.2.3"},
           {"ddtags", DDTAGS},
           {"ok-global", 100},
           {"ok-logger", 200},
           {"ok-message", 300}
       }})},

      {"M no longer include custom attributes W attributes have been deleted",
       [](dd_logging_t* logging) {
         // Set "foo":100 globally
         dd_attribute_t int_100 = dd_attribute_int(100);
         dd_logging_add_attribute(logging, "foo", &int_100);
         dd_attribute_free(&int_100);
       },
       [](dd_logging_t*, dd_logger_t* logger) {
         // Set "bar":200 on logger
         dd_attribute_t int_200 = dd_attribute_int(200);
         dd_logger_add_attribute(logger, "bar", &int_200);
         dd_attribute_free(&int_200);
       },
       [](dd_logging_t* logging, dd_logger_t* logger) {
         // Set "baz":300 on an object attribute that will be passed with log calls
         dd_attribute_t obj = dd_attribute_object(1);
         dd_attribute_t int_300 = dd_attribute_int(300);
         dd_attribute_object_property_set(&obj, "baz", &int_300);
         dd_attribute_free(&int_300);

         // Emit a single log message
         dd_logger_info(logger, "alpha", &obj);

         // Next: delete our three custom attributes and emit another message
         dd_logging_remove_attribute(logging, "foo");
         dd_logger_remove_attribute(logger, "bar");
         dd_attribute_object_property_delete(&obj, "baz");
         dd_logger_info(logger, "bravo", &obj);

         dd_attribute_free(&obj);
       },
       // Our first event should have all three custom attribute values, while the
       // second event should have none
       nlohmann::json{
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "info"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "alpha"},
               {"logger.version", "1.2.3"},
               {"ddtags", DDTAGS},
               {"foo", 100},
               {"bar", 200},
               {"baz", 300}
           },
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "info"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "bravo"},
               {"logger.version", "1.2.3"},
               {"ddtags", DDTAGS}
           }
       }}
  };
  for (const auto& tt : tests) {
    DYNAMIC_SECTION(tt.name) {
      // Given a core
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      dd_core_t* core = CoreTestHarness::WrapForC(test);

      // And a registered logging feature that our test has configured
      dd_logging_t* logging = dd_logging_init(core);
      tt.pre_logger_init(logging);

      // And a logger that our test has configured
      dd_logger_t* logger = dd_logger_create(logging, nullptr);
      tt.post_logger_init(logging, logger);

      // When we start the core, run our logging tests, and stop the core
      dd_core_start(core);
      tt.while_running(logging, logger);
      dd_core_stop(core);

      // Then the SDK should have sent the expected set of events
      REQUIRE(test.client.requests.size() == 1);
      REQUIRE(MergeJsonArrays(test.client.requests) == tt.want_request_body);

      // Cleanup
      dd_logger_destroy(logger);
      dd_logging_destroy(logging);
      dd_core_destroy(core);
    }
  }
}

TEST_CASE("dd_logger tags", "[unit][logging][c-api]") {
  // These tests register the logging feature and initialize a logger, then they
  // exercise tag manipulation while the core is running and examine the resulting HTTP
  // request to see if it matches want_request_body
  struct TestParams {
    std::string_view name;
    std::function<void(dd_logger_t*)> setup;
    std::function<void(dd_logger_t*)> while_running;
    nlohmann::json want_request_body;
  };
  // Constructs the expected ddtags value by appending custom tags to the standard set
  auto ddtags_with = [](std::string_view suffix) {
    return nlohmann::json(DDTAGS.get<std::string>() + "," + std::string(suffix));
  };
  std::vector<TestParams> tests = {
      {"M append custom tag to ddtags W dd_logger_add_tag called with pre-formatted "
       "entry",
       [](dd_logger_t* logger) {
         // Add a tag using the pre-formatted '<key>:<value>' form
         dd_logger_add_tag(logger, "foo:hello");
       },
       [](dd_logger_t* logger) { dd_logger_info(logger, "hello", nullptr); },
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", "1.2.3"},
           {"ddtags", ddtags_with("foo:hello")}
       }})},

      {"M append custom tag to ddtags W dd_logger_add_tag_kv called with separate key "
       "and value",
       [](dd_logger_t* logger) {
         // Add a tag using the separate key/value form
         dd_logger_add_tag_kv(logger, "bar", "world");
       },
       [](dd_logger_t* logger) { dd_logger_info(logger, "hello", nullptr); },
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", "1.2.3"},
           {"ddtags", ddtags_with("bar:world")}
       }})},

      {"M remove exact tag W dd_logger_remove_tag is called with matching entry",
       [](dd_logger_t* logger) {
         // Add two tags with the same key but different values
         dd_logger_add_tag(logger, "foo:1");
         dd_logger_add_tag(logger, "foo:2");
       },
       [](dd_logger_t* logger) {
         // Log a message with both tags present, then remove one and log again
         dd_logger_info(logger, "alpha", nullptr);
         dd_logger_remove_tag(logger, "foo:1");
         dd_logger_info(logger, "bravo", nullptr);
       },
       nlohmann::json{
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "info"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "alpha"},
               {"logger.version", "1.2.3"},
               {"ddtags", ddtags_with("foo:1,foo:2")}
           },
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "info"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "bravo"},
               {"logger.version", "1.2.3"},
               {"ddtags", ddtags_with("foo:2")}
           }
       }},

      {"M remove all tags with key W dd_logger_remove_tags_with_key is called",
       [](dd_logger_t* logger) {
         // Add two tags under 'foo' and one under 'bar'
         dd_logger_add_tag(logger, "foo:1");
         dd_logger_add_tag(logger, "foo:2");
         dd_logger_add_tag(logger, "bar:3");
       },
       [](dd_logger_t* logger) {
         // Log a message with all tags, then remove all 'foo' entries and log again
         dd_logger_info(logger, "alpha", nullptr);
         dd_logger_remove_tags_with_key(logger, "foo");
         dd_logger_info(logger, "bravo", nullptr);
       },
       nlohmann::json{
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "info"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "alpha"},
               {"logger.version", "1.2.3"},
               {"ddtags", ddtags_with("foo:1,foo:2,bar:3")}
           },
           nlohmann::json{
               {"os", OS_PROPERTIES},
               {"device", DEVICE_PROPERTIES},
               {"status", "info"},
               {"service", "mock-service"},
               {"date", "2023-11-14T22:13:20.000Z"},
               {"message", "bravo"},
               {"logger.version", "1.2.3"},
               {"ddtags", ddtags_with("bar:3")}
           }
       }},
  };
  for (const auto& tt : tests) {
    DYNAMIC_SECTION(tt.name) {
      // Given a core
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      dd_core_t* core = CoreTestHarness::WrapForC(test);

      // And a registered logging feature and a logger that our test has configured
      dd_logging_t* logging = dd_logging_init(core);
      dd_logger_t* logger = dd_logger_create(logging, nullptr);
      tt.setup(logger);

      // When we start the core, run our tag manipulation tests, and stop the core
      dd_core_start(core);
      tt.while_running(logger);
      dd_core_stop(core);

      // Then the SDK should have sent the expected set of events
      REQUIRE(test.client.requests.size() == 1);
      REQUIRE(MergeJsonArrays(test.client.requests) == tt.want_request_body);

      // Cleanup
      dd_logger_destroy(logger);
      dd_logging_destroy(logging);
      dd_core_destroy(core);
    }
  }
}

TEST_CASE("dd_logger error details", "[unit][logging][c-api]") {
  // Given a started core with a logger
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  dd_core_t* core = CoreTestHarness::WrapForC(test);
  dd_logging_t* logging = dd_logging_init(core);
  dd_logger_t* logger = dd_logger_create(logging, nullptr);
  dd_core_start(core);

  SECTION("M include error.kind and error.stack W dd_logger_error called with both") {
    // When dd_logger_error is called with a fully-populated error struct
    dd_log_error_t err{"SomeException", "frame 0\nframe 1"};
    dd_logger_error(logger, "msg", &err, nullptr);
    dd_core_stop(core);

    // Then the resulting event carries both error fields
    REQUIRE(test.client.requests.size() == 1);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["error.kind"] == "SomeException");
    REQUIRE(events[0]["error.stack"] == "frame 0\nframe 1");
  }

  SECTION(
      "M include error.kind and error.stack W dd_logger_critical called with both"
  ) {
    // When dd_logger_critical is called with a fully-populated error struct
    dd_log_error_t err{"SomeException", "frame 0\nframe 1"};
    dd_logger_critical(logger, "msg", &err, nullptr);
    dd_core_stop(core);

    // Then the resulting event carries both error fields
    REQUIRE(test.client.requests.size() == 1);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() == 1);
    REQUIRE(events[0]["error.kind"] == "SomeException");
    REQUIRE(events[0]["error.stack"] == "frame 0\nframe 1");
  }

  SECTION("M omit error fields W err is NULL") {
    // When dd_logger_error is called with a null error pointer
    dd_logger_error(logger, "msg", nullptr, nullptr);
    dd_core_stop(core);

    // Then the resulting event carries neither error field
    REQUIRE(test.client.requests.size() == 1);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() == 1);
    REQUIRE(!events[0].contains("error.kind"));
    REQUIRE(!events[0].contains("error.stack"));
  }

  SECTION("M omit error fields W err has empty strings") {
    // When dd_logger_error is called with an error struct whose fields are both empty
    dd_log_error_t err{"", ""};
    dd_logger_error(logger, "msg", &err, nullptr);
    dd_core_stop(core);

    // Then the resulting event carries neither error field
    REQUIRE(test.client.requests.size() == 1);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() == 1);
    REQUIRE(!events[0].contains("error.kind"));
    REQUIRE(!events[0].contains("error.stack"));
  }

  SECTION("M omit error fields W level is below error") {
    // When dd_logger_log is called at WARN with error details provided
    dd_log_error_t err{"SomeException", "trace"};
    dd_logger_log(logger, DD_LOG_LEVEL_WARN, "msg", &err, nullptr);
    dd_core_stop(core);

    // Then the resulting event carries neither error field, since the level guard in
    // Logger::Log strips them before they reach the context thread
    REQUIRE(test.client.requests.size() == 1);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() == 1);
    REQUIRE(!events[0].contains("error.kind"));
    REQUIRE(!events[0].contains("error.stack"));
  }

  // Cleanup
  dd_logger_destroy(logger);
  dd_logging_destroy(logging);
  dd_core_destroy(core);
}

TEST_CASE("dd_logger thread-safety", "[unit][logging][c-api][thread-safety]") {
  // Given a running SDK with logging enabled
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  dd_core_t* core = CoreTestHarness::WrapForC(test);
  dd_logging_t* logging = dd_logging_init(core);
  dd_core_start(core);

  // And 20 threads that each create their own loggers and emit 100 messages each
  auto threads = RunParallel(20, [logging](size_t thread_id) {
    const std::string name = "logger-" + std::to_string(thread_id);
    dd_logger_config_t config;
    dd_logger_config_init(&config);
    dd_logger_config_set_service(&config, "svc");
    dd_logger_config_set_name(&config, name.c_str());
    dd_logger_t* logger = dd_logger_create(logging, &config);
    for (int i = 0; i < 100; ++i) {
      const std::string message = "message " + std::to_string(i);
      dd_logger_info(logger, message.c_str(), nullptr);
    }
    dd_logger_destroy(logger);
  });

  // And meanwhile, in the main thread, we continually modify a global 'xyz' attribute
  // value, occasionally deleting it
  for (int i = 0; i < 2000; i++) {
    // We need to delete the attribute in the main thread 90% of the time in order
    // for it to have no value in the upload thread ~50% of the time
    if (i % 10 < 9) {
      dd_logging_remove_attribute(logging, "xyz");
    } else {
      dd_attribute_t value;
      const std::string s = "loop-" + std::to_string(i);
      value = dd_attribute_string(s.c_str());
      dd_logging_add_attribute(logging, "xyz", &value);
      dd_attribute_free(&value);
    }
  }

  // And we join on all threads and stop the core
  for (auto& thread : threads) {
    thread.join();
  }
  dd_core_stop(core);

  // Then we should have sent at least one request
  REQUIRE(!test.client.requests.empty());
  auto events = MergeJsonArrays(test.client.requests);

  // And some events have no global 'xyz' attribute, since sometimes it was deleted
  size_t num_events_without_xyz =
      std::count_if(events.begin(), events.end(), [](const nlohmann::json& obj) {
        REQUIRE(obj.is_object());
        return !obj.contains("xyz");
      });
  STRICT_THREADING_REQUIRE(num_events_without_xyz > 0);

  // And some events have a 'xyz' value 'loop-%d'
  size_t num_events_with_valid_xyz =
      std::count_if(events.begin(), events.end(), [](const nlohmann::json& obj) {
        REQUIRE(obj.is_object());
        if (!obj.contains("xyz")) {
          return false;
        }
        std::string xyz = obj.at("xyz");
        REQUIRE(xyz.find("loop-") == 0);
        return true;
      });
  STRICT_THREADING_REQUIRE(num_events_with_valid_xyz > 0);

  // And every single event fits one of those cases: either it has no 'xyz' value or
  // it has a well-formed 'loop-<i>' string
  REQUIRE(num_events_without_xyz + num_events_with_valid_xyz == events.size());

  // Cleanup
  dd_logging_destroy(logging);
  dd_core_destroy(core);
}
