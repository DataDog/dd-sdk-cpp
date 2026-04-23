// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "datadog/core.h"
#include "datadog/logging.h"

#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

#include "support/catch.hpp"
#include "support/core.hpp"
#include "support/diagnostics.hpp"
#include "support/filesystem.hpp"
#include "support/tempdir.hpp"

TEST_CASE("dd_core_config internal_options source/sdk_version", "[unit][core][c-api]") {
  SECTION("M apply source override W internal_options.source set") {
    // Given a config with a source override in internal_options
    dd_core_config_t config;
    dd_core_config_init(&config, "token", "service", "env");
    dd_core_config_set_event_storage_location(&config, ".");
    config.internal_options.source = "unity";

    // When we create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then the core is valid (source override doesn't prevent initialization)
    REQUIRE(core != nullptr);
    dd_core_destroy(core);
  }
}

TEST_CASE("dd_core null safety", "[unit][core][c-api]") {
  SECTION("M safely do nothing W target object is null") {
    REQUIRE(dd_core_create(nullptr) == nullptr);
    dd_core_destroy(nullptr);

    dd_core_set_tracking_consent(nullptr, DD_TRACKING_CONSENT_GRANTED);
    REQUIRE(dd_core_start(nullptr) == false);
    dd_core_stop(nullptr);
  }
}

TEST_CASE("dd_core_config validation", "[unit][core][c-api][writes-to-cwd-datadog]") {
  // Capture diagnostic messages that would be printed to stderr by default
  DiagnosticMessageBuffer diagnostics;

  SECTION(
      "M accept config but print event-storage-location warning W dd_core_config_init "
      "called"
  ) {
    // Given a config struct that's been initialized with the bare-minimum set of
    // values, without an explicit event storage path being configured
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");
    diagnostics.ConfigureC(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get a valid dd_core_t
    REQUIRE(core != nullptr);
    dd_core_destroy(core);

    // And a single warning is emitted to let us know that the default behavior of
    // writing events to $PWD/.datadog is inadvisable
    REQUIRE(diagnostics.TotalSize() == 1);
    REQUIRE(diagnostics.warning.size() == 1);
    REQUIRE(
        diagnostics.warning[0] ==
        "Events will be stored within .datadog/ in the current working directory: "
        "application should call SetEventStorageLocation to specify a suitable "
        "application-specific directory where .datadog/ can be created"
    );

    // And we find that a .datadog/main/<pid>/ directory was populated anyway, which we
    // clean up to keep the working directory clean
    PruneDotDatadogDir();
  }

  SECTION(
      "M accept config w/o warning W dd_core_config_init + "
      "dd_core_config_set_event_storage_location called"
  ) {
    // Given a config struct that's been initialized with the bare-minimum set of
    // values, with an event storage path explicitly configured
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");
    dd_core_config_set_event_storage_location(&config, ".");
    diagnostics.ConfigureC(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get a valid dd_core_t
    REQUIRE(core != nullptr);
    dd_core_destroy(core);

    // And no diagnostic warnings/errors are emitted
    REQUIRE(diagnostics.TotalSize() == 0);

    // And we find (and remove) a .datadog/main/<pid>/ directory
    PruneDotDatadogDir();
  }

  SECTION("M accept config W version is 1") {
    // Given a properly-initialized config struct
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", "my-env");
    dd_core_config_set_event_storage_location(&config, ".");
    diagnostics.ConfigureC(&config);

    // When we explicitly set the struct version to 1
    config.version = 1;

    // And we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get a valid dd_core_t, even in a future where CORE_CONFIG_VERSION has
    // been bumped and is no longer 1
    REQUIRE(core != nullptr);
    dd_core_destroy(core);
    REQUIRE(diagnostics.TotalSize() == 0);

    // And we find (and remove) a .datadog/main/<pid>/ directory
    PruneDotDatadogDir();
  }

  SECTION("M reject config W version not set") {
    // Given a config struct that's just zero-filled
    dd_core_config_t config;
    std::memset(&config, 0, sizeof(config));
    diagnostics.ConfigureC(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get null
    REQUIRE(core == nullptr);

    // And we receive no diagnostic messages: our handler callback is stored in the
    // config struct, which the SDK can't be sure was properly initialized
    REQUIRE(diagnostics.TotalSize() == 0);
  }

  SECTION("M reject config W client_token is missing") {
    // Given a config that's missing a client_token value
    auto client_token = GENERATE("", nullptr);
    dd_core_config_t config;
    dd_core_config_init(&config, client_token, "my-service", "my-env");
    diagnostics.ConfigureC(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get null
    REQUIRE(core == nullptr);

    // And we receive a diagnostic error
    REQUIRE(diagnostics.TotalSize() == 1);
    REQUIRE(diagnostics.error.size() == 1);
    REQUIRE(
        diagnostics.error[0] ==
        "SDK initialization failed: application must supply a non-empty 'client_token' "
        "value in dd_core_config_t"
    );
  }

  SECTION("M reject config W service is missing") {
    // Given a config that's missing a service value
    auto service = GENERATE("", nullptr);
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", service, "my-env");
    diagnostics.ConfigureC(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get null
    REQUIRE(core == nullptr);

    // And we receive a diagnostic error
    REQUIRE(diagnostics.TotalSize() == 1);
    REQUIRE(diagnostics.error.size() == 1);
    REQUIRE(
        diagnostics.error[0] ==
        "SDK initialization failed: application must supply a non-empty 'service' "
        "value in dd_core_config_t"
    );
  }

  SECTION("M reject config W env is missing") {
    // Given a config that's missing an env value
    auto env = GENERATE("", nullptr);
    dd_core_config_t config;
    dd_core_config_init(&config, "my-client-token", "my-service", env);
    diagnostics.ConfigureC(&config);

    // When we attempt to create a core from that config
    dd_core_t* core = dd_core_create(&config);

    // Then we get null
    REQUIRE(core == nullptr);

    // And we receive a diagnostic error
    REQUIRE(diagnostics.TotalSize() == 1);
    REQUIRE(diagnostics.error.size() == 1);
    REQUIRE(
        diagnostics.error[0] ==
        "SDK initialization failed: application must supply a non-empty 'env' value in "
        "dd_core_config_t"
    );
  }
}

TEST_CASE("dd_core event storage location", "[unit][core][c-api]") {
  // These tests use actual filesystem operations (in conjunction with TempDirectory)
  // to validate that the SDK writes events to the appropriate directory as configured.
  // We set up the SDK with logging, then produce a log event, then exit without
  // uploading any events; and then we assert that we got the expected set of filesystem
  // modifications and diagnostic output.
  struct TestParams {
    std::string_view name;
    std::function<void(dd_core_config_t*, TempDirectory&)> setup_func;
    std::function<void(bool, const DiagnosticMessageBuffer&, TempDirectory&)>
        assert_func;
  };
  std::vector<TestParams> tests = {
      {"M create new .datadog subdir and write events to it W storage path is tmpdir "
       "and no .datadog subdir exists {pending consent}",
       [](dd_core_config_t* config, TempDirectory& tmpdir) {
         // When we configure the SDK to use our temp directory for storage, without
         // explicitly setting tracking consent
         dd_core_config_set_event_storage_location(config, tmpdir.path.c_str());
       },
       [](bool started,
          const DiagnosticMessageBuffer& diagnostics,
          TempDirectory& tmpdir) {
         // Then the SDK should start properly
         REQUIRE(started);

         // And it should produce no diagnostic errors
         REQUIRE(diagnostics.warning.empty());
         REQUIRE(diagnostics.error.empty());

         // And it should have created a $tmpdir/.datadog subdirectory and written our
         // single log event to a batch file in the appropriate feature/consent subdir
         const std::string path =
             ".datadog/main/" + GetPidString() + "/logs/intermediate-v1";
         REQUIRE(tmpdir.DirectoryExists(path));
         auto filenames = tmpdir.ReadDirectoryContents(path);
         REQUIRE(filenames.size() == 1);

         // And that file should contain the text of our log message
         auto file = tmpdir.ReadFileContents(path + "/" + filenames[0]);
         REQUIRE(file.find("hello") != std::string::npos);
       }},

      {"M create new .datadog subdir and write events to it W storage path is tmpdir "
       "and no .datadog subdir exists {granted consent}",
       [](dd_core_config_t* config, TempDirectory& tmpdir) {
         // When we configure the SDK to use our temp directory for storage, and we set
         // our initial tracking consent to 'granted'
         dd_core_config_set_event_storage_location(config, tmpdir.path.c_str());
         dd_core_config_set_initial_tracking_consent(
             config, DD_TRACKING_CONSENT_GRANTED
         );
       },
       [](bool started,
          const DiagnosticMessageBuffer& diagnostics,
          TempDirectory& tmpdir) {
         // Then the SDK should start properly
         REQUIRE(started);

         // And it should produce no diagnostic errors
         REQUIRE(diagnostics.warning.empty());
         REQUIRE(diagnostics.error.empty());

         // And it should have created a $tmpdir/.datadog subdirectory and written our
         // single log event to a batch file in the appropriate feature/consent subdir
         const std::string path = ".datadog/main/" + GetPidString() + "/logs/v1";
         REQUIRE(tmpdir.DirectoryExists(path));
         auto filenames = tmpdir.ReadDirectoryContents(path);
         REQUIRE(filenames.size() == 1);

         // And that file should contain the text of our log message
         auto file = tmpdir.ReadFileContents(path + "/" + filenames[0]);
         REQUIRE(file.find("hello") != std::string::npos);
       }},

      {"M use existing .datadog subdir W storage path is tmpdir and tmpdir/.datadog "
       "exists",
       [](dd_core_config_t* config, TempDirectory& tmpdir) {
         // When we create an existing .datadog/ subdirectory within tmpdir
         tmpdir.Mkdirs(".datadog");
         REQUIRE(tmpdir.DirectoryExists(".datadog"));

         // And we configure the SDK to use our temp directory for storage, and we set
         // our initial tracking consent to 'granted'
         dd_core_config_set_event_storage_location(config, tmpdir.path.c_str());
         dd_core_config_set_initial_tracking_consent(
             config, DD_TRACKING_CONSENT_GRANTED
         );
       },
       [](bool started,
          const DiagnosticMessageBuffer& diagnostics,
          TempDirectory& tmpdir) {
         // Then the SDK should start properly
         REQUIRE(started);

         // And it should produce no diagnostic errors
         REQUIRE(diagnostics.warning.empty());
         REQUIRE(diagnostics.error.empty());

         // And it should have created logs/v1 within the existing $tmpdir/.datadog
         // directory and written our single log event to a batch file there
         const std::string path = ".datadog/main/" + GetPidString() + "/logs/v1";
         REQUIRE(tmpdir.DirectoryExists(path));
         auto filenames = tmpdir.ReadDirectoryContents(path);
         REQUIRE(filenames.size() == 1);

         // And that file should contain the text of our log message
         auto file = tmpdir.ReadFileContents(path + "/" + filenames[0]);
         REQUIRE(file.find("hello") != std::string::npos);
       }},

      {"M fail with error on SDK start W storage path is tmpdir and tmpdir/.datadog "
       "can not be created due to existing file at that path",
       [](dd_core_config_t* config, TempDirectory& tmpdir) {
         // When we create an existing .datadog *file* within tmpdir, preventing a root
         // storage directory from being created on SDK start
         tmpdir.WriteFile(".datadog", "oh no, conflict");
         REQUIRE(tmpdir.FileExists(".datadog"));
         REQUIRE(!tmpdir.DirectoryExists(".datadog"));

         // And we configure the SDK to use our temp directory for storage
         dd_core_config_set_event_storage_location(config, tmpdir.path.c_str());
       },
       [](bool started,
          const DiagnosticMessageBuffer& diagnostics,
          TempDirectory& tmpdir) {
         // Then the SDK should fail to start, as it can't initialize the root storage
         // directory
         REQUIRE(!started);

         // And it should produce diagnostic errors describing the problem
         REQUIRE(diagnostics.warning.empty());
         REQUIRE(diagnostics.error.size() == 2);
         REQUIRE(diagnostics.error[0].find("AlreadyExists") != std::string::npos);
         REQUIRE(
             diagnostics.error[1] ==
             "Core initialization failed: could not initialize SDK storage"
         );

         // And our storage directory should remain unchanged
         REQUIRE(!tmpdir.DirectoryExists(".datadog/logs"));
         REQUIRE(tmpdir.FileExists(".datadog"));
       }},

      {"M fail with error on SDK start W storage path is nonexistent directory",
       [](dd_core_config_t* config, TempDirectory& tmpdir) {
         // When we configure the SDK to use a nonexistent directory for storage
         auto nonexistent_path = std::filesystem::path(tmpdir.path) / "nonexistent-dir";
         dd_core_config_set_event_storage_location(
             config, nonexistent_path.string().c_str()
         );
       },
       [](bool started,
          const DiagnosticMessageBuffer& diagnostics,
          TempDirectory& tmpdir) {
         // Then the SDK should fail to start, as it can't initialize the root storage
         // directory
         REQUIRE(!started);

         // And it should produce diagnostic errors describing the problem
         REQUIRE(diagnostics.warning.empty());
         REQUIRE(diagnostics.error.size() == 2);
         REQUIRE(diagnostics.error[0].find("DoesNotExist") != std::string::npos);
         REQUIRE(
             diagnostics.error[1] ==
             "Core initialization failed: could not initialize SDK storage"
         );

         // And our temp directory should remain unchanged
         REQUIRE(!tmpdir.DirectoryExists("nonexistent-dir"));
       }},
  };

  for (const auto& tt : tests) {
    DYNAMIC_SECTION(tt.name) {
      // Given a temporary directory that we can use for transient storage
      TempDirectory tmpdir;

      // And an SDK configuration with all required values for normal operation
      dd_core_config_t config;
      dd_core_config_init(&config, "my-client-token", "my-service", "my-env");
      DiagnosticMessageBuffer diagnostics;
      diagnostics.ConfigureC(&config);

      // When we configure the event storage location used by this test case
      tt.setup_func(&config, tmpdir);

      // And then we register logging, create a logger, and attempt to start the core
      dd_core_t* core = dd_core_create(&config);
      dd_logging_t* logging = dd_logging_init(core);
      dd_logger_t* logger = dd_logger_create(logging, NULL);
      const bool started = dd_core_start(core);

      // Next, assuming the core was started: When we emit a single log message and stop
      // the core, which should result in a single event being generated and flushed to
      // disk (log calls produce events synchronously, and stopping the core drains the
      // storage queue)
      if (started) {
        dd_logger_info(logger, "hello");
        dd_core_stop(core);
      }

      // Then our test case's assert function validates the expected results for:
      // - Whether the SDK started successfully
      // - What diagnostic errors were emitted, if any
      // - What changes were made to the filesystem (in our temp directory), if any
      tt.assert_func(started, diagnostics, tmpdir);

      // Cleanup
      dd_logger_destroy(logger);
      dd_logging_destroy(logging);
      dd_core_destroy(core);
    }
  }
}

TEST_CASE(
    "dd_core_config internal_options overrides in network requests",
    "[unit][core][c-api]"
) {
  SECTION(
      "M use overridden source in request URL and headers W internal_options.source set"
  ) {
    // Given a core configured with a source override in internal_options
    CoreConfig config = MOCK_CORE_CONFIG;
    config.Internal_SetSource("unity");
    auto test = CoreTestHarness::Init(config);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, nullptr);
    dd_core_start(core);

    // When we emit a log event and stop the core
    dd_logger_info(logger, "hello");
    dd_core_stop(core);

    // Then the HTTP request reflects the overridden source
    REQUIRE(test.client.requests.size() == 1);
    const auto& req = test.client.requests.front();
    REQUIRE(req.url.find("ddsource=unity") != std::string::npos);
    REQUIRE(req.headers.find("DD-EVP-ORIGIN: unity\r\n") != std::string::npos);

    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }

  SECTION(
      "M use overridden sdk_version in request headers W internal_options.sdk_version "
      "set"
  ) {
    // Given a core configured with a sdk_version override in internal_options
    CoreConfig config = MOCK_CORE_CONFIG;
    config.Internal_SetSdkVersion("99.0.0");
    auto test = CoreTestHarness::Init(config);
    dd_core_t* core = CoreTestHarness::WrapForC(test);
    dd_logging_t* logging = dd_logging_init(core);
    dd_logger_t* logger = dd_logger_create(logging, nullptr);
    dd_core_start(core);

    // When we emit a log event and stop the core
    dd_logger_info(logger, "hello");
    dd_core_stop(core);

    // Then the HTTP request reflects the overridden SDK version
    REQUIRE(test.client.requests.size() == 1);
    const auto& req = test.client.requests.front();
    REQUIRE(req.headers.find("DD-EVP-ORIGIN-VERSION: 99.0.0\r\n") != std::string::npos);

    dd_logger_destroy(logger);
    dd_logging_destroy(logging);
    dd_core_destroy(core);
  }
}
