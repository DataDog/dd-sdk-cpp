// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/core.hpp"
#include "datadog/logging.hpp"

#include "support/catch.hpp"
#include "support/core.hpp"
#include "support/diagnostics.hpp"
#include "support/tempdir.hpp"

using namespace datadog;

TEST_CASE("Core null safety", "[unit][core][cpp-api]") {
  SECTION("M safely do nothing W this wraps nullptr") {
    // Given a CoreConfig that lacks required parameters
    CoreConfig config("", "", "");

    // When we create a Core from that config
    auto core = Core::Create(config);

    // Then we get a valid object that handles all member functions calls as a no-op
    REQUIRE(core != nullptr);
    REQUIRE(core->Start() == false);
    core->SetTrackingConsent(datadog::TrackingConsent::Granted);
    core->Stop();
  }
}

TEST_CASE("Core validation", "[unit][core][cpp-api]") {
  // Capture diagnostic messages that would be printed to stderr by default
  DiagnosticMessageBuffer diagnostics;

  SECTION(
      "M accept config but print event-storage-location warning W initialized with "
      "required values"
  ) {
    // Given a config struct that's been initialized with the bare-minimum set of
    // values, without an explicit event storage path being configured
    CoreConfig config("my-client-token", "my-service", "my-env");
    diagnostics.ConfigureCpp(config);

    // When we attempt to create a core from that config
    auto core = Core::Create(config);

    // Then a single warning is emitted to let us know that the default behavior of
    // writing events to $PWD/.datadog is inadvisable
    REQUIRE(diagnostics.TotalSize() == 1);
    REQUIRE(diagnostics.warning.size() == 1);
    REQUIRE(
        diagnostics.warning[0] ==
        "Events will be stored within .datadog/ in the current working directory: "
        "application should call SetEventStorageLocation to specify a suitable "
        "application-specific directory where .datadog/ can be created"
    );
  }

  SECTION(
      "M accept config w/o warning W initialized with required values and "
      "SetEventStorageLocation called"
  ) {
    // Given a config struct that's been initialized with the bare-minimum set of
    // values, with an event storage path explicitly configured
    CoreConfig config("my-client-token", "my-service", "my-env");
    config.SetEventStorageLocation(".");
    diagnostics.ConfigureCpp(config);

    // When we attempt to create a core from that config
    auto core = Core::Create(config);

    // Then no diagnostic warnings/errors are emitted
    REQUIRE(diagnostics.TotalSize() == 0);
  }

  SECTION("M reject config W client_token is missing") {
    // Given a config struct that's missing a client_token value
    CoreConfig config("", "my-service", "my-env");
    diagnostics.ConfigureCpp(config);

    // When we attempt to create a core from that config
    auto core = Core::Create(config);

    // Then we receive a diagnostic error
    REQUIRE(diagnostics.TotalSize() == 1);
    REQUIRE(diagnostics.error.size() == 1);
    REQUIRE(
        diagnostics.error[0] ==
        "SDK initialization failed: application must supply a non-empty 'client_token' "
        "value in CoreConfig"
    );
  }

  SECTION("M reject config W service is missing") {
    // Given a config struct that's missing a service value
    CoreConfig config("my-client-token", "", "my-env");
    diagnostics.ConfigureCpp(config);

    // When we attempt to create a core from that config
    auto core = Core::Create(config);

    // Then we receive a diagnostic error
    REQUIRE(diagnostics.TotalSize() == 1);
    REQUIRE(diagnostics.error.size() == 1);
    REQUIRE(
        diagnostics.error[0] ==
        "SDK initialization failed: application must supply a non-empty 'service' "
        "value in CoreConfig"
    );
  }

  SECTION("M reject config W env is missing") {
    // Given a config struct that's missing an env value
    CoreConfig config("my-client-token", "my-service", "");
    diagnostics.ConfigureCpp(config);

    // When we attempt to create a core from that config
    auto core = Core::Create(config);

    // Then we receive a diagnostic error
    REQUIRE(diagnostics.TotalSize() == 1);
    REQUIRE(diagnostics.error.size() == 1);
    REQUIRE(
        diagnostics.error[0] ==
        "SDK initialization failed: application must supply a non-empty 'env' value in "
        "CoreConfig"
    );
  }
}

TEST_CASE("Core event storage location", "[unit][core][c-api]") {
  // These tests use actual filesystem operations (in conjunction with TempDirectory)
  // to validate that the SDK writes events to the appropriate directory as configured.
  // We set up the SDK with logging, then produce a log event, then exit without
  // uploading any events; and then we assert that we got the expected set of filesystem
  // modifications and diagnostic output.
  struct TestParams {
    std::string_view name;
    std::function<void(CoreConfig&, TempDirectory&)> setup_func;
    std::function<void(bool, const DiagnosticMessageBuffer&, TempDirectory&)>
        assert_func;
  };
  std::vector<TestParams> tests = {
      {"M create new .datadog subdir and write events to it W storage path is tmpdir "
       "and no .datadog subdir exists {pending consent}",
       [](CoreConfig& config, TempDirectory& tmpdir) {
         // When we configure the SDK to use our temp directory for storage, without
         // explicitly setting tracking consent
         config.SetEventStorageLocation(tmpdir.path);
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
         const std::string path = ".datadog/logs/intermediate-v1";
         REQUIRE(tmpdir.DirectoryExists(path));
         auto filenames = tmpdir.ReadDirectoryContents(path);
         REQUIRE(filenames.size() == 1);

         // And that file should contain the text of our log message
         auto file = tmpdir.ReadFileContents(path + "/" + filenames[0]);
         REQUIRE(file.find("hello") != std::string::npos);
       }},

      {"M create new .datadog subdir and write events to it W storage path is tmpdir "
       "and no .datadog subdir exists {granted consent}",
       [](CoreConfig& config, TempDirectory& tmpdir) {
         // When we configure the SDK to use our temp directory for storage, and we set
         // our initial tracking consent to 'granted'
         config.SetEventStorageLocation(tmpdir.path);
         config.SetInitialTrackingConsent(TrackingConsent::Granted);
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
         const std::string path = ".datadog/logs/v1";
         REQUIRE(tmpdir.DirectoryExists(path));
         auto filenames = tmpdir.ReadDirectoryContents(path);
         REQUIRE(filenames.size() == 1);

         // And that file should contain the text of our log message
         auto file = tmpdir.ReadFileContents(path + "/" + filenames[0]);
         REQUIRE(file.find("hello") != std::string::npos);
       }},

      {"M use existing .datadog subdir W storage path is tmpdir and tmpdir/.datadog "
       "exists",
       [](CoreConfig& config, TempDirectory& tmpdir) {
         // When we create an existing .datadog/ subdirectory within tmpdir
         tmpdir.Mkdirs(".datadog");
         REQUIRE(tmpdir.DirectoryExists(".datadog"));

         // And we configure the SDK to use our temp directory for storage, and we set
         // our initial tracking consent to 'granted'
         config.SetEventStorageLocation(tmpdir.path);
         config.SetInitialTrackingConsent(TrackingConsent::Granted);
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
         const std::string path = ".datadog/logs/v1";
         REQUIRE(tmpdir.DirectoryExists(path));
         auto filenames = tmpdir.ReadDirectoryContents(path);
         REQUIRE(filenames.size() == 1);

         // And that file should contain the text of our log message
         auto file = tmpdir.ReadFileContents(path + "/" + filenames[0]);
         REQUIRE(file.find("hello") != std::string::npos);
       }},

      {"M fail with error on SDK start W storage path is tmpdir and tmpdir/.datadog "
       "can not be created due to existing file at that path",
       [](CoreConfig& config, TempDirectory& tmpdir) {
         // When we create an existing .datadog *file* within tmpdir, preventing a root
         // storage directory from being created on SDK start
         tmpdir.WriteFile(".datadog", "oh no, conflict");
         REQUIRE(tmpdir.FileExists(".datadog"));
         REQUIRE(!tmpdir.DirectoryExists(".datadog"));

         // And we configure the SDK to use our temp directory for storage
         config.SetEventStorageLocation(tmpdir.path);
       },
       [](bool started,
          const DiagnosticMessageBuffer& diagnostics,
          TempDirectory& tmpdir) {
         // Then the SDK should fail to start, as it can't initialize the root storage
         // directory
         REQUIRE(!started);

         // And it should produce a diagnostic error describing the problem
         REQUIRE(diagnostics.warning.empty());
         REQUIRE(diagnostics.error.size() == 1);
         REQUIRE(
             diagnostics.error[0].find(
                 "SDK initialization failed: event storage subsystem could not be "
                 "initialized: root storage path is occupied by a file"
             ) == 0
         );

         // And our storage directory should remain unchanged
         REQUIRE(!tmpdir.DirectoryExists(".datadog/logs"));
         REQUIRE(tmpdir.FileExists(".datadog"));
       }},

      {"M fail with error on SDK start W storage path is nonexistent directory",
       [](CoreConfig& config, TempDirectory& tmpdir) {
         // When we configure the SDK to use a nonexistent directory for storage
         auto nonexistent_path = std::filesystem::path(tmpdir.path) / "nonexistent-dir";
         config.SetEventStorageLocation(nonexistent_path.string());
       },
       [](bool started,
          const DiagnosticMessageBuffer& diagnostics,
          TempDirectory& tmpdir) {
         // Then the SDK should fail to start, as it can't initialize the root storage
         // directory
         REQUIRE(!started);

         // And it should produce a diagnostic error describing the problem
         REQUIRE(diagnostics.warning.empty());
         REQUIRE(diagnostics.error.size() == 1);
         REQUIRE(
             diagnostics.error[0].find(
                 "SDK initialization failed: event storage subsystem could not be "
                 "initialized: failed to create root storage directory"
             ) == 0
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
      CoreConfig config("my-client-token", "my-service", "my-env");
      DiagnosticMessageBuffer diagnostics;
      diagnostics.ConfigureCpp(config);

      // When we configure the event storage location used by this test case
      tt.setup_func(config, tmpdir);

      // And then we register logging, create a logger, and attempt to start the core
      auto core = Core::Create(config);
      auto logging = Logging::Register(core);
      auto logger = logging->CreateLogger();
      const bool started = core->Start();

      // Next, assuming the core was started: When we emit a single log message and stop
      // the core, which should result in a single event being generated and flushed to
      // disk (log calls produce events synchronously, and stopping the core drains the
      // storage queue)
      if (started) {
        logger->Info("hello");
        core->Stop();
      }

      // Then our test case's assert function validates the expected results for:
      // - Whether the SDK started successfully
      // - What diagnostic errors were emitted, if any
      // - What changes were made to the filesystem (in our temp directory), if any
      tt.assert_func(started, diagnostics, tmpdir);
    }
  }
}

TEST_CASE("CoreConfig AddAdditionalConfiguration", "[unit][core][cpp-api]") {
  SECTION("M store _dd.source and apply it W AddAdditionalConfiguration called") {
    // Given a config with a _dd.source override
    CoreConfig config("token", "service", "env");
    config.SetEventStorageLocation(".");
    config.AddAdditionalConfiguration("_dd.source", "unity");

    // When we create a core from that config (no crash / rejection)
    auto core = Core::Create(config);

    // Then the core is valid
    REQUIRE(core != nullptr);
  }

  SECTION("M overwrite existing key W AddAdditionalConfiguration called twice") {
    CoreConfig config("token", "service", "env");
    config.AddAdditionalConfiguration("_dd.source", "unity");
    config.AddAdditionalConfiguration("_dd.source", "flutter");

    // The second call should silently overwrite the first; creation should succeed
    config.SetEventStorageLocation(".");
    auto core = Core::Create(config);
    REQUIRE(core != nullptr);
  }
}

TEST_CASE(
    "CoreConfig additional_configuration overrides in network requests",
    "[unit][core][cpp-api]"
) {
  SECTION("M use overridden source in request URL and headers W _dd.source set") {
    // Given a core configured with a _dd.source override
    CoreConfig config = MOCK_CORE_CONFIG;
    config.AddAdditionalConfiguration("_dd.source", "unity");
    auto test = CoreTestHarness::Init(config);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    core->Start();

    // When we emit a log event and stop the core
    logging->CreateLogger()->Info("hello");
    core->Stop();

    // Then the HTTP request reflects the overridden source
    REQUIRE(test.client.requests.size() == 1);
    const auto& req = test.client.requests.front();
    REQUIRE(req.url.find("ddsource=unity") != std::string::npos);
    REQUIRE(req.headers.find("DD-EVP-ORIGIN: unity\n") != std::string::npos);
  }

  SECTION("M use overridden sdk_version in request headers W _dd.sdk_version set") {
    // Given a core configured with a _dd.sdk_version override
    CoreConfig config = MOCK_CORE_CONFIG;
    config.AddAdditionalConfiguration("_dd.sdk_version", "99.0.0");
    auto test = CoreTestHarness::Init(config);
    auto core = CoreTestHarness::WrapForCpp(test);
    auto logging = Logging::Register(core);
    core->Start();

    // When we emit a log event and stop the core
    logging->CreateLogger()->Info("hello");
    core->Stop();

    // Then the HTTP request reflects the overridden SDK version
    REQUIRE(test.client.requests.size() == 1);
    const auto& req = test.client.requests.front();
    REQUIRE(req.headers.find("DD-EVP-ORIGIN-VERSION: 99.0.0\n") != std::string::npos);
  }
}

