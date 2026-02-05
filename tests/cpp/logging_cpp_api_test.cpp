// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <nlohmann/json.hpp>
#include <vector>

#include "datadog/logging.hpp"

#include "datadog/impl/core/version.hpp"
#include "datadog/impl/features/logging/logging.hpp"

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

TEST_CASE("Logging null safety", "[unit][logging][cpp-api]") {
  SECTION("M safely do nothing W this wraps nullptr") {
    // Given both a valid Core interface that has no valid implementation pointer, as
    // well as a straight-up null pointer to a Core interface
    const datadog::CoreConfig invalid_config("", "", "");
    std::shared_ptr<Core> noop_core = Core::Create(invalid_config);
    std::shared_ptr<Core> null_core;
    std::vector<std::shared_ptr<Core>> cores = {noop_core, null_core};
    for (std::shared_ptr<Core>& core : cores) {
      // When we register the logging feature on an invalid core
      auto logging = Logging::Register(core);

      // Then we get a valid object that handles all member function calls as a no-op
      REQUIRE(logging != nullptr);
      logging->AddAttribute("foo", Attribute::Int(1));
      logging->RemoveAttribute("bar");

      // And: When we try to create a logger from a no-op logging interface
      auto logger = logging->CreateLogger();

      // Then we get a valid no-op logger
      REQUIRE(logger != nullptr);
      logger->AddAttribute("foo", Attribute::Int(2));
      logger->RemoveAttribute("bar");
      logger->Info("hello");
    }
  }
}

TEST_CASE("Logging::Register", "[unit][logging][cpp-api]") {
  SECTION("M return valid feature W registered with core") {
    // Given a valid core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);

    // When we register the logging feature
    auto logging = Logging::Register(core);

    // Then we get a valid Logging interface
    REQUIRE(logging != nullptr);
  }

  SECTION("M return no-op interface W feature registered multiple times") {
    // Given a valid core that already has logging enabled
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    REQUIRE(logging != nullptr);

    // When we attempt register the logging feature a second time
    auto other_logging = Logging::Register(core);

    // Then we get a valid pointer to a no-op logging interface
    // TODO: Surface some indication of whether a call to the C++ API succeeded (and
    // gave you a valid, functional object) or failed (and gave you a no-op interface)
    REQUIRE(other_logging != nullptr);
    other_logging->CreateLogger()->Debug("no-op");
  }
}

TEST_CASE("Logging::CreateLogger", "[unit][logging][cpp-api]") {
  SECTION("M return valid logger W initialized with valid logging feature") {
    // Given a valid Logging feature and a Core that's already started
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    REQUIRE(core->Start());

    // When we call CreateLogger
    auto logger = logging->CreateLogger(
        LoggerConfig().SetService("my-cool-service").SetName("my-cool-logger")
    );

    // Then we get a valid logger
    REQUIRE(logger != nullptr);

    // And we get no diagnostic errors or warnings
    test.Diagnostics().RequireNoWarnings().RequireNoErrors();
  }

  SECTION("M return valid logger W core not yet started") {
    // Given a valid Logging feature and Core that's not yet started
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);

    // When we call CreateLogger
    auto logger = logging->CreateLogger(
        LoggerConfig().SetService("my-cool-service").SetName("my-cool-logger")
    );

    // Then we get a valid logger, even though the SDK isn't running yet
    REQUIRE(logger != nullptr);
    test.Diagnostics().RequireNoWarnings().RequireNoErrors();
  }

  SECTION("M use default config W no config is provided") {
    // Given a valid Logging feature and Core
    auto test = CoreTestHarness::Init();
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);

    // When we call CreateLogger with an implicit default config
    auto logger = logging->CreateLogger();

    // Then we get a valid logger
    REQUIRE(logger != nullptr);

    // And we get no diagnostic errors or warnings
    test.Diagnostics().RequireNoWarnings().RequireNoErrors();
  }
}

TEST_CASE("Logging argument validation", "[unit][logging][cpp-api]") {
  // Given a series of tests consisting of a set of API calls and the warnings and/or
  // errors we expect to get in response
  struct TestParams {
    std::string_view name;
    std::function<void(std::shared_ptr<Logging>&)> func;
    std::vector<std::string_view> want_warnings;
    std::vector<std::string_view> want_errors;
  };
  std::vector<TestParams> tests = {

      // === Basic usage with no errors/warnings expected ===

      {"M print no warnings or errors W used normally",
       [](std::shared_ptr<Logging>& logging) {
         // Add and remove global attributes
         logging->AddAttribute("foo", Attribute::Int(100));
         logging->AddAttribute("bar", Attribute::Int(200));
         logging->RemoveAttribute("foo");

         // Create a logger with the default config
         auto logger_a = logging->CreateLogger();

         // Create a logger with a custom config
         auto logger_b = logging->CreateLogger(
             LoggerConfig()
                 .SetRemoteSampleRate(100.0f)
                 .SetService("my-service")
                 .SetName("my-logger")
                 .SetRemoteLogThreshold(LogLevel::Debug)
                 .SetInitialAttributeCapacity(8)
         );

         // Add and remove logger attributes
         logger_a->AddAttribute("bar", Attribute::String("hello"));
         logger_a->AddAttribute("baz", Attribute::String("world"));
         logger_a->RemoveAttribute("bar");

         // Log a message
         logger_a->Warn("hello");

         // Log a message with attributes
         Attribute message_attributes = Attribute::Object(1);
         message_attributes.SetObjectProperty("bar", Attribute::String("world"));
         logger_b->Info("goodbye", message_attributes);
       },
       // All of the above should complete with 0 warnings and 0 errors
       {},
       {}},
  };
  for (const auto& tt : tests) {
    DYNAMIC_SECTION(tt.name) {
      // Given a started core with the logging feature registered
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      auto core = CoreTestHarness::WrapForCpp(test);
      auto logging = Logging::Register(core);
      REQUIRE(core->Start());

      // When we execute our test function to exercise the logging API
      tt.func(logging);
      core->Stop();

      // Then we get the expected set of diagnostic warnings and errors
      REQUIRE(test.c_diagnostics.size() == 0);
      DiagnosticAsserts diagnostics = test.Diagnostics();
      diagnostics.RequireWarnings(tt.want_warnings);
      diagnostics.RequireErrors(tt.want_errors);
    }
  }
}

TEST_CASE("Logger::Log", "[unit][logging][cpp-api]") {
  SECTION("M a emit log message W log is called") {
    // Given a valid logger and a started core
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    auto logger = logging->CreateLogger();
    core->Start();

    // When we emit a bunch of log messages and stop the core
    logger->Log(LogLevel::Info, "hello");
    logger->Debug("gubed");
    logger->Info("ofni");
    logger->Notice("eciton");
    logger->Warn("nraw");
    logger->Error("rorre");
    logger->Critical("lacitirc");
    core->Stop();

    // Then the SDK sends a request that contains our log messages
    REQUIRE(test.client.requests.size() == 1);
    const MockHttpRequest& req = test.client.requests.front();
    REQUIRE(
        req.url == "https://browser-intake-datadoghq.com/api/v2/logs?ddsource=unity"
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
                {"logger.version", impl::SDK_VERSION}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "debug"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "gubed"},
                {"logger.version", impl::SDK_VERSION}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "info"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "ofni"},
                {"logger.version", impl::SDK_VERSION}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "notice"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "eciton"},
                {"logger.version", impl::SDK_VERSION}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "warn"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "nraw"},
                {"logger.version", impl::SDK_VERSION}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "error"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "rorre"},
                {"logger.version", impl::SDK_VERSION}
            },
            nlohmann::json{
                {"os", OS_PROPERTIES},
                {"device", DEVICE_PROPERTIES},
                {"status", "critical"},
                {"service", "mock-service"},
                {"date", "2023-11-14T22:13:20.000Z"},
                {"message", "lacitirc"},
                {"logger.version", impl::SDK_VERSION}
            },
        }
    );
  }

  SECTION("M do nothing W log is called before start") {
    // Given a valid logger and a core that's not yet started
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    auto logger = logging->CreateLogger();

    // When we emit a log message prior to starting the core
    logger->Info("ofni");

    // And we start and stop the core thereafter
    core->Start();
    core->Stop();

    // Then no events are generated and no requests are sent, as any API calls
    // made before core start are simply no-ops
    REQUIRE(test.storage.FindFiles("logging/v1").size() == 0);
    REQUIRE(test.storage.GetNumFilesDeleted() == 0);
    REQUIRE(test.client.requests.size() == 0);
  }

  SECTION("M do nothing W log is called after shutdown") {
    // Given a valid logger and a started core
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    auto logger = logging->CreateLogger();
    core->Start();

    // When we stop the core
    core->Stop();

    // And make a log call thereafter
    logger->Info("hello");

    // Then no events are generated and no requests are sent, as any API calls
    // made after core stop are simply no-ops
    REQUIRE(test.storage.FindFiles("logging/v1").size() == 0);
    REQUIRE(test.storage.GetNumFilesDeleted() == 0);
    REQUIRE(test.client.requests.size() == 0);
  }

  SECTION("M do nothing W log is called on orphaned logger") {
    std::shared_ptr<Logger> logger;
    {
      // Given a valid logger (which will outlive the core) and a started core
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      auto core = CoreTestHarness::WrapForCpp(test);
      auto logging = Logging::Register(core);
      logger = logging->CreateLogger();
      core->Start();

      // When we stop the core, and allow the logging feature and the core to go
      // out of scope and be destroyed
      core->Stop();
    }

    // And we make a log call thereafter
    logger->Info("hello");

    // Then this is still a safe no-op, as the logger communicates the event to the
    // logging feature via a weak_ptr
  }

  SECTION("M continue normally W logging feature is destroyed prior to core stop") {
    // Given a core
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    std::shared_ptr<Logger> logger;
    {
      // When we create a logger that will outlive our reference to the logging
      // feature
      auto logging = Logging::Register(core);
      logger = logging->CreateLogger();

      // And we start the core
      core->Start();

      // And the logging feature leaves scope, destroying our API-level reference
    }

    // And we emit a log message and stop the core
    logger->Info("hello");
    core->Stop();

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
            {"logger.version", impl::SDK_VERSION}
        }})
    );
  }

  SECTION("M use name and service W provided via logger config") {
    // Given a logger config that sets 'name' and 'service'
    LoggerConfig config;
    config.SetService("overridden-service");
    config.SetName("my-logger");

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    auto logger = logging->CreateLogger(config);
    core->Start();

    // When we emit a log message and stop the core
    logger->Info("hello");
    core->Stop();

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
            {"logger.version", impl::SDK_VERSION}
        }})
    );
  }

  SECTION("M use default name and service W explicitly set to empty string") {
    // Given a logger config that sets 'name' and 'service' to valid strings, then sets
    // them to empty string
    LoggerConfig config;
    config.SetService("unused-service-name");
    config.SetName("unused-logger-name");
    config.SetService("");
    config.SetName("");

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    auto logger = logging->CreateLogger(config);
    core->Start();

    // When we emit a log message and stop the core
    logger->Info("hello");
    core->Stop();

    // Then the resulting log event contains our default service and logger names, just
    // as if we hadn't overridden them
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
            {"logger.version", impl::SDK_VERSION}
        }})
    );
  }

  SECTION("M emit only messages at or above threshold W log threshold is set") {
    // Given a logger config that sets a remote log threshold at 'error'
    LoggerConfig config;
    config.SetRemoteLogThreshold(LogLevel::Error);

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    auto logger = logging->CreateLogger(config);
    core->Start();

    // When we emit 10 warning messages, 10 error messages, and 10 critical
    // messages, then stop the core
    for (int i = 0; i < 10; i++) {
      logger->Warn("perhaps this is bad");
      logger->Error("this is definitely bad");
      logger->Critical("hold the phone, this is very bad");
    }
    core->Stop();

    // Then we should have sent exactly 20 events
    REQUIRE(test.client.requests.size() > 0);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() == 20);

    // And those events should consist of 10 errors and 10 critical errors
    auto has_status = [](const std::string_view status, const nlohmann::json& obj) {
      REQUIRE(obj.is_object());
      REQUIRE(obj.at("status").is_string());
      return obj.at("status") == status;
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
    LoggerConfig config;
    config.SetRemoteSampleRate(50.0f);

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    auto logger = logging->CreateLogger(config);
    core->Start();

    // When we emit 1000 log messages and stop the core
    for (int i = 0; i < 1000; i++) {
      logger->Info("hello");
    }
    core->Stop();

    // Then we should have sent ~500 log messages, +/- 60
    REQUIRE(test.client.requests.size() > 0);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() >= 440);
    REQUIRE(events.size() <= 560);
  }

  SECTION("M emit no messages W sampling rate is 0") {
    // Given a logger config that sets a remote sampling rate of 0%
    LoggerConfig config;
    config.SetRemoteSampleRate(0.0f);

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    auto logger = logging->CreateLogger(config);
    core->Start();

    // When we emit 1000 log messages and stop the core
    for (int i = 0; i < 1000; i++) {
      logger->Info("hello");
    }
    core->Stop();

    // Then we should have sent no log messages whatsoever
    REQUIRE(test.client.requests.size() == 0);
  }

  SECTION("M apply sampling rate after log threshold W both are set") {
    // Given a logger config that sets a remote sampling rate of 50% and a remote
    // log threshold at 'info' and above
    LoggerConfig config;
    config.SetRemoteSampleRate(50.0f);
    config.SetRemoteLogThreshold(LogLevel::Info);

    // And a started core running a logger initialized from that config
    auto test = CoreTestHarness::Init();
    test.clock.FreezeAtMilliseconds(1700000000000);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    auto logger = logging->CreateLogger(config);
    core->Start();

    // When we emit 500 debug message and 500 log messages, then stop the core
    for (int i = 0; i < 500; i++) {
      logger->Debug("this is not very important");
      logger->Info("this might be important");
    }
    core->Stop();

    // Then we should have sent ~250 log messages, +/- 30
    REQUIRE(test.client.requests.size() > 0);
    auto events = MergeJsonArrays(test.client.requests);
    REQUIRE(events.size() >= 220);
    REQUIRE(events.size() <= 280);

    // And they all should be 'info' messages
    const size_t num_info =
        std::count_if(events.begin(), events.end(), [](const nlohmann::json& obj) {
          REQUIRE(obj.is_object());
          REQUIRE(obj.at("status").is_string());
          return obj.at("status") == "info";
        });
    REQUIRE(num_info == events.size());
  }
}

TEST_CASE("Logger attributes", "[unit][logging][cpp-api]") {
  // These tests register the logging feature and initialize a logger, then they
  // exercise the logger while the core is running and examine the resulting HTTP
  // request to see if it matches want_request_body
  struct TestParams {
    std::string_view name;
    std::function<void(Logging&)> pre_logger_init;
    std::function<void(Logging&, Logger&)> post_logger_init;
    std::function<void(Logging&, Logger&)> while_running;
    nlohmann::json want_request_body;
  };
  std::vector<TestParams> tests = {
      {"M include custom attribute W set globally on logging feature",
       [](Logging& logging) {
         // Set "foo":100 globally before logger is created
         logging.AddAttribute("foo", Attribute::Int(100));
       },
       [](Logging& logging, Logger&) {
         // Set "bar":"yes" globally after logger is created
         logging.AddAttribute("bar", Attribute::String("yes"));
       },
       [](Logging&, Logger& logger) {
         // Emit a message from our logger
         logger.Info("hello");
       },
       // Event should include "foo":100,"bar":"yes"
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", impl::SDK_VERSION},
           {"foo", 100},
           {"bar", "yes"}
       }})},

      {"M include custom attribute W set on logger",
       [](Logging& logging) {
         // Set "foo":100 globally
         logging.AddAttribute("foo", Attribute::Int(100));
       },
       [](Logging&, Logger& logger) {
         // Set "bar":200 on logger
         logger.AddAttribute("bar", Attribute::Int(200));
       },
       [](Logging&, Logger& logger) {
         // Emit a message from our logger
         logger.Info("hello");
       },
       // Event should include "foo":100,"bar":200
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", impl::SDK_VERSION},
           {"foo", 100},
           {"bar", 200}
       }})},

      {"M override global property W logger property has same name",
       [](Logging& logging) {
         // Set "foo":100 globally
         logging.AddAttribute("foo", Attribute::Int(100));
       },
       [](Logging&, Logger& logger) {
         // Set "foo":200 on logger
         logger.AddAttribute("foo", Attribute::Int(200));
       },
       [](Logging&, Logger& logger) {
         // Emit a message from our logger
         logger.Info("hello");
       },
       // Event should include "foo":200
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", impl::SDK_VERSION},
           {"foo", 200}
       }})},

      {"M include custom attribute W set on message",
       [](Logging& logging) {
         // Set "foo":100 globally
         logging.AddAttribute("foo", Attribute::Int(100));
       },
       [](Logging&, Logger& logger) {
         // Set "bar":200 on logger
         logger.AddAttribute("bar", Attribute::Int(200));
       },
       [](Logging&, Logger& logger) {
         // Set "baz":300 on an object attribute that will be passed with log calls
         Attribute obj = Attribute::Object(1);
         obj.SetObjectProperty("baz", Attribute::Int(300));

         // Emit messages from our logger, testing all functions for coverage
         logger.Log(LogLevel::Info, "hello", obj);
         logger.Debug("gubed", obj);
         logger.Info("ofni", obj);
         logger.Notice("eciton", obj);
         logger.Warn("nraw", obj);
         logger.Error("rorre", obj);
         logger.Critical("lacitirc", obj);
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
               {"logger.version", impl::SDK_VERSION},
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
               {"logger.version", impl::SDK_VERSION},
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
               {"logger.version", impl::SDK_VERSION},
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
               {"logger.version", impl::SDK_VERSION},
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
               {"logger.version", impl::SDK_VERSION},
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
               {"logger.version", impl::SDK_VERSION},
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
               {"logger.version", impl::SDK_VERSION},
               {"foo", 100},
               {"bar", 200},
               {"baz", 300}
           }
       }},

      {"M override global and logger properties W message property has same name",
       [](Logging& logging) {
         // Set "foo":100,"baz":100 globally
         logging.AddAttribute("foo", Attribute::Int(100));
         logging.AddAttribute("baz", Attribute::Int(100));
       },
       [](Logging&, Logger& logger) {
         // Set "bar":200,"baz":200 on logger
         logger.AddAttribute("bar", Attribute::Int(200));
         logger.AddAttribute("baz", Attribute::Int(200));
       },
       [](Logging&, Logger& logger) {
         // Log a message with "foo":300,"bar":400
         Attribute obj = Attribute::Object(2);
         obj.SetObjectProperty("foo", Attribute::Int(300));
         obj.SetObjectProperty("bar", Attribute::Int(400));
         logger.Info("hello", obj);
       },
       // Event should include "foo":300,"baz":200,"bar":400
       nlohmann::json::array({nlohmann::json{
           {"os", OS_PROPERTIES},
           {"device", DEVICE_PROPERTIES},
           {"status", "info"},
           {"service", "mock-service"},
           {"date", "2023-11-14T22:13:20.000Z"},
           {"message", "hello"},
           {"logger.version", impl::SDK_VERSION},
           {"foo", 300},
           {"baz", 200},
           {"bar", 400}
       }})},

      {"M not override attributes W property name is reserved",
       [](Logging& logging) {
         // Set "status":100,"ok-global":100 globally
         logging.AddAttribute("status", Attribute::Int(100));
         logging.AddAttribute("ok-global", Attribute::Int(100));
       },
       [](Logging&, Logger& logger) {
         // Set "service":200,"ok-logger":200 on logger
         logger.AddAttribute("service", Attribute::Int(200));
         logger.AddAttribute("ok-logger", Attribute::Int(200));
       },
       [](Logging&, Logger& logger) {
         // Log a message with "message":300,"_dd":300,"ok-message":300
         Attribute obj = Attribute::Object(3);
         obj.SetObjectProperty("message", Attribute::Int(300));
         obj.SetObjectProperty("_dd", Attribute::Int(300));
         obj.SetObjectProperty("ok-message", Attribute::Int(300));
         logger.Info("hello", obj);
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
           {"logger.version", impl::SDK_VERSION},
           {"ok-global", 100},
           {"ok-logger", 200},
           {"ok-message", 300}
       }})},

      {"M no longer include custom attributes W attributes have been deleted",
       [](Logging& logging) {
         // Set "foo":100 globally
         logging.AddAttribute("foo", Attribute::Int(100));
       },
       [](Logging&, Logger& logger) {
         // Set "bar":200 on logger
         logger.AddAttribute("bar", Attribute::Int(200));
       },
       [](Logging& logging, Logger& logger) {
         // Set "baz":300 on an object attribute that will be passed with log calls
         Attribute obj = Attribute::Object(1);
         obj.SetObjectProperty("baz", Attribute::Int(300));

         // Emit a single log message
         logger.Info("alpha", obj);

         // Next: delete our three custom attributes and emit another message
         logging.RemoveAttribute("foo");
         logger.RemoveAttribute("bar");
         obj.DeleteObjectProperty("baz");
         logger.Info("bravo", obj);
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
               {"logger.version", impl::SDK_VERSION},
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
               {"logger.version", impl::SDK_VERSION},
           }
       }}
  };
  for (const auto& tt : tests) {
    DYNAMIC_SECTION(tt.name) {
      // Given a core
      auto test = CoreTestHarness::Init();
      test.clock.FreezeAtMilliseconds(1700000000000);
      auto core = CoreTestHarness::WrapForCpp(test);

      // And a registered logging feature that our test has configured
      auto logging = Logging::Register(core);
      tt.pre_logger_init(*logging);

      // And a logger that our test has configured
      auto logger = logging->CreateLogger();
      tt.post_logger_init(*logging, *logger);

      // When we start the core, run our logging tests, and stop the core
      core->Start();
      tt.while_running(*logging, *logger);
      core->Stop();

      // Then the SDK should have sent the expected set of events
      REQUIRE(test.client.requests.size() == 1);
      REQUIRE(MergeJsonArrays(test.client.requests) == tt.want_request_body);
    }
  }
}

TEST_CASE("Logger thread-safety", "[unit][logging][cpp-api][thread-safety]") {
  // Given a running SDK with logging enabled
  auto test = CoreTestHarness::Init();
  test.clock.FreezeAtMilliseconds(1700000000000);
  auto core = CoreTestHarness::WrapForCpp(test);
  auto logging = Logging::Register(core);
  core->Start();

  // And 20 threads that each create their own loggers and emit 100 messages each
  auto threads = RunParallel(20, [logging](size_t thread_id) {
    const std::string name = "logger-" + std::to_string(thread_id);
    auto logger = logging->CreateLogger(LoggerConfig().SetService("svc").SetName(name));
    for (int i = 0; i < 100; ++i) {
      const std::string message = "message " + std::to_string(i);
      logger->Info(message);
    }
  });

  // And meanwhile, in the main thread, we continually modify a global 'xyz' attribute
  // value, occasionally deleting it
  for (int i = 0; i < 2000; i++) {
    // We need to delete the attribute in the main thread 90% of the time in order
    // for it to have no value in the upload thread ~50% of the time
    if (i % 10 < 9) {
      logging->RemoveAttribute("xyz");
    } else {
      const std::string s = "loop-" + std::to_string(i);
      logging->AddAttribute("xyz", Attribute::String(s));
    }
  }

  // And we join on all threads and stop the core
  for (auto& thread : threads) {
    thread.join();
  }
  core->Stop();

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
}
