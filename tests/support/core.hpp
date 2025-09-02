#pragma once

#include <limits>

#include "core/core.hpp"

#include "mock/clock.hpp"
#include "mock/filesystem.hpp"
#include "mock/http_client.hpp"

using namespace datadog;

/**
 * Default SDK configuration used in tests.
 */
static const CoreConfig MOCK_CORE_CONFIG{
    TrackingConsent::Granted,
    Site::us1,
    "mock-client-token",
    "mock-service",
    "mock-env",
    "mock-application-version",
    BatchSize::Small,
    UploadFrequency::Frequent,
    BatchProcessingLevel::Low,
    0,
};

/**
 * Encapsulates test setup, initializing a working Core implementation with mock
 * implementations of platform subsystems.
 *
 * Allows you to register features (real or mock), start and stop the core, and examine
 * the resulting filesystem state and network requests, while exercising the actual
 * implementation of the Core and its storage and upload threads.
 */
struct CoreTestHarness
{
    impl::Core core;
    MockClock& clock;
    MockStorageDirectory& storage;
    MockHttpClient& client;

    explicit CoreTestHarness(
        impl::Core&& in_core,
        MockClock& in_clock,
        MockStorageDirectory& in_storage,
        MockHttpClient& in_client
    )
        : core(std::move(in_core))
        , clock(in_clock)
        , storage(in_storage)
        , client(in_client)
    {
    }

    static CoreTestHarness Init(bool flush_http_requests = true)
    {
        // Create mock implementations of required core subsystems
        auto _clock = std::make_unique<MockClock>();
        auto _storage_root = std::make_unique<MockStorageDirectory>();
        auto _http = std::make_unique<MockHttpSubsystem>();

        // Capture references to the underlying objects before we transfer ownership out
        // of these unique_ptrs
        MockClock& clock = *_clock;
        MockStorageDirectory& storage = *_storage_root;
        MockHttpSubsystem& http = *_http;

        // Create the core, giving the core ownership of injected subsystems
        CoreConfig config = MOCK_CORE_CONFIG;
        config.num_http_requests_per_feature_to_flush_on_stop =
            flush_http_requests ? std::numeric_limits<size_t>::max() : 0;
        impl::Core core(
            config,
            impl::CoreSubsystems(
                std::move(_clock), std::move(_storage_root), std::move(_http)
            )
        );

        // Initialize the core: this should always succeed in tests
        if (!core.Init())
        {
            assert(false && "core init failed in test setup");
        }

        // The core should have created an HTTP client on init; get a reference to it
        assert(http.clients.size() == 1 && "core did not create 1 mock HTTP client");
        MockHttpClient* client_ptr = http.clients[0];

        // Return a struct that contains all the state we need in order to test - and
        // examine the results of - code that interfaces with the core
        return CoreTestHarness(std::move(core), clock, storage, *client_ptr);
    }
};
