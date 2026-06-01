// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <limits>
#include <memory>

#include "datadog/core.h"
#include "datadog/core.hpp"

#include "datadog/c/core_glue.hpp"
#include "datadog/impl/core/core.hpp"

#include "mock/clock.hpp"
#include "mock/filesystem.hpp"
#include "mock/http_client.hpp"
#include "mock/system_info.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;

static void on_c_diagnostic(const dd_diagnostic_message_t* message, void* userdata);

/**
 * Default SDK configuration used in tests.
 */
static const CoreConfig MOCK_CORE_CONFIG =
    CoreConfig("mock-client-token", "mock-service", "mock-env")
        .SetEventStorageLocation("app")
        .SetApplicationVersion("mock-application-version")
        .SetBatchSize(BatchSize::Small)
        .SetUploadFrequency(UploadFrequency::Frequent)
        .SetBatchProcessingLevel(BatchProcessingLevel::Low)
        .Internal_SetSource("mock-source")
        .Internal_SetSdkVersion("1.2.3");

/**
 * Encapsulates test setup, initializing a working Core implementation with mock
 * implementations of platform subsystems.
 *
 * Allows you to register features (real or mock), start and stop the core, and examine
 * the resulting filesystem state and network requests, while exercising the actual
 * implementation of the Core and its storage and upload threads.
 */
struct CoreTestHarness {
  // Owns the core initially: may be moved out for API tests, but the non-owning ref
  // `Core& core` will still permit access
  std::unique_ptr<impl::Core> _core;

  impl::Core& core;
  MockClock& clock;
  MockFilesystem& fs;
  MockHttpClient& client;

  std::vector<dd_diagnostic_message_t> c_diagnostics;
  std::vector<DiagnosticMessage> cpp_diagnostics;

  explicit CoreTestHarness(
      std::unique_ptr<impl::Core>&& in_core,
      MockClock& in_clock,
      MockFilesystem& in_fs,
      MockHttpClient& in_client
  )
      : _core(std::move(in_core)),
        core(std::ref(*_core)),
        clock(in_clock),
        fs(in_fs),
        client(in_client) {}

  static CoreTestHarness Init(
      CoreConfig config,
      TrackingConsent tracking_consent = TrackingConsent::Granted,
      bool flush_http_requests = true
  ) {
    // Create mock implementations of required core subsystems
    auto _clock = std::make_unique<MockClock>();
    auto _fs = std::make_unique<MockFilesystem>();
    auto _http = std::make_unique<MockHttpSubsystem>();
    auto _system_info = std::make_unique<MockSystemInfo>();

    // Capture references to the underlying objects before we transfer ownership out of
    // these unique_ptrs
    MockClock& clock = *_clock;
    MockFilesystem& fs = *_fs;
    MockHttpSubsystem& http = *_http;

    // MOCK_CORE_CONFIG configures the SDK to store data in a directory called 'app';
    // create that directory on the mock filesystem
    fs.Mkdirs("app");

    // Create the core, giving the core ownership of injected subsystems
    if (flush_http_requests) {
      config.Internal_FlushHttpRequestsOnStop();
    }
    auto core = std::make_unique<impl::Core>(
        config,
        tracking_consent,
        impl::CoreSubsystems(
            std::move(_clock), std::move(_fs), std::move(_http), std::move(_system_info)
        )
    );

    // Initialize the core: this should always succeed in tests
    if (!core->Init()) {
      assert(false && "core init failed in test setup");
    }

    // The core should have created an HTTP client on init; get a reference to it
    assert(http.clients.size() == 1 && "core did not create 1 mock HTTP client");
    MockHttpClient* client_ptr = http.clients[0];

    // Return a struct that contains all the state we need in order to test - and
    // examine the results of - code that interfaces with the core
    return CoreTestHarness(std::move(core), clock, fs, *client_ptr);
  }

  static CoreTestHarness Init(bool flush_http_requests = true) {
    return Init(MOCK_CORE_CONFIG, TrackingConsent::Granted, flush_http_requests);
  }

  /**
   * Initializes a CoreTestHarness for use in C API tests.
   */
  static dd_core_t* WrapForC(CoreTestHarness& test) {
    // Steal the impl::Core from the CoreTestHarness so it can be owned by the C API
    // interface
    return new dd_core_t(
        std::move(test._core),
        impl::DiagnosticLogger::FromC(on_c_diagnostic, &test, DD_DIAGNOSTIC_LEVEL_DEBUG)
    );
  }

  /**
   * Initializes a CoreTestHarness for use in C++ API tests.
   */
  static std::shared_ptr<Core> WrapForCpp(CoreTestHarness& test) {
    return std::make_shared<Core>(
        std::move(test._core),
        [&](const DiagnosticMessage& message) {
          test.cpp_diagnostics.push_back(message);
        },
        DiagnosticLevel::Debug,
        Core::PrivateCtorTag{}
    );
  }

  DiagnosticAsserts Diagnostics() const {
    return DiagnosticAsserts{c_diagnostics, cpp_diagnostics};
  }
};

static void on_c_diagnostic(const dd_diagnostic_message_t* message, void* userdata) {
  CoreTestHarness& test = *reinterpret_cast<CoreTestHarness*>(userdata);
  test.c_diagnostics.push_back(*message);
}
