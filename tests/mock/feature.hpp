#pragma once

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/core.hpp"
#include "core/feature.hpp"
#include "core/storage.hpp"
#include "platform/http_writer.hpp" // TODO: Move StringWriter out of platform

#include "mock/filesystem.hpp"
#include "mock/http_client.hpp"

using namespace datadog;

/**
 * Persistent copy of a TLV Block read from a file by a MockFeature.
 */
struct TLVBlockCopy
{
    impl::TLVBlockType type;
    std::string data;

    TLVBlockCopy(impl::TLVBlockType in_type, std::string_view in_data)
        : type(in_type)
        , data(in_data)
    {}
};

/**
 * Description of a single call made to UploadThread_PrepareReport in MockFeature.
 */
struct MockReport
{
    std::string url;
    std::string headers;
    std::string body;

    std::vector<TLVBlockCopy> blocks_read;
    std::optional<impl::BatchReadError> last_read_error;
    std::optional<impl::TLVBlockReadResultType> last_batch_read_result;
};

/**
 * Mock Feature implementation. Can be registered, can produce events using the callback
 * provided via OnCoreStarted(), can prepare reports from TLV batches; with all relevant
 * operations recorded for examination.
 */
class MockFeature : public impl::Feature
{
public:
    // Store feature details for easy initialization/registration
    impl::FeatureId id;
    std::string name;

    // Test can set these values manually after ctor
    std::string path{ "/api/v1/test" };
    std::string content_type{ "text/plain" };
    std::string feature_headers{ "" };

    // Calls to start/stop will be recorded
    int num_start_calls{ 0 };
    int num_stop_calls{ 0 };

    // Each report generated will be recorded here for tests to examine
    std::vector<MockReport> reports;

    MockFeature(impl::FeatureId in_id, std::string_view in_name)
        : id(in_id)
        , name(in_name)
    {}

    // Implement the basic interface used by Core

    impl::FeatureId GetId() const override
    {
        return id;
    }

    virtual std::string_view GetName() const override
    {
        return name;
    }

    virtual void Start() override
    {
        num_start_calls++;
    }

    virtual void Stop() override
    {
        num_stop_calls++;
    }

    /**
     * Allows tests to arbitrarily generate events.
     */
    void GenerateEvent(impl::Block event, impl::Block event_metadata = {})
    {
        WriteEvent(event, event_metadata);
    }

    /**
     * Default implementation to record reports generated under test.
     */
    virtual std::optional<impl::Report> UploadThread_PrepareReport(
        const impl::CoreContext& context,
        impl::BatchReader& reader
    ) override
    {
        // Use a report struct to contain all the relevant data about this call
        MockReport report;

        // Build URL and headers based on configuration
        context.BuildRequestURL(path, true, report.url);
        context.BuildRequestHeaders(content_type, feature_headers, report.headers);

        // Read all TLV blocks into a vector, serially
        bool read_ok = true;
        while (true)
        {
            // If read failed, stop reading and store the error
            auto result = reader.ReadNext();
            if (!result)
            {
                report.last_read_error = result.error();
                read_ok = false;
                break;
            }

            // Block read OK; copy data to vector
            report.blocks_read.emplace_back(result->type, result->block);

            // If this was the last block, we're done
            if (result->eof)
            {
                break;
            }
        }

        // If any reads failed, produce no report and early-out
        if (!read_ok)
        {
            return std::nullopt;
        }

        // Build full HTTP request body sychronously
        report.body = BuildRequestBody(report.blocks_read);

        // Move report into our vector, both so tests can examine it, and so our HTTP
        // client can reference the request body beyond the lifetime of this function
        const MockReport& stored = reports.emplace_back(std::move(report));

        // Return the resulting report
        return impl::Report{ stored.url,
                             stored.headers,
                             platform::StringWriter{ stored.body } };
    }

    // Override to implement custom block processing
    virtual std::string BuildRequestBody(const std::vector<TLVBlockCopy>& blocks)
    {
        (void)blocks;
        return "{}";
    }
};

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
    MockStorageDirectory& storage;
    MockHttpClient& client;

    explicit CoreTestHarness(
        impl::Core&& in_core,
        MockStorageDirectory& in_storage,
        MockHttpClient& in_client
    )
        : core(std::move(in_core))
        , storage(in_storage)
        , client(in_client)
    {}

    static CoreTestHarness Init()
    {
        // Create mock implementations of required core subsystems
        auto _storage_root = std::make_unique<MockStorageDirectory>();
        auto _http = std::make_unique<MockHttpSubsystem>();

        // Capture references to the underlying objects before we transfer ownership out
        // of these unique_ptrs
        MockStorageDirectory& storage = *_storage_root;
        MockHttpSubsystem& http = *_http;

        // Create the core, giving the core ownership of injected subsystems
        impl::Core core(
            MOCK_CORE_CONFIG,
            impl::CoreSubsystems(std::move(_storage_root), std::move(_http))
        );

        // Initialize the core: this should always succeed in tests
        if (!core.Init())
        {
            assert(false && "core init failed in test setup");
        }

        // The core should have created an HTTP client on init; get a reference to it
        assert(http.clients.size() == 1 && "core did not create 1 mock HTTP client");
        MockHttpClient* client_addr = http.clients[0];

        // Return a struct that contains all the state we need in order to test - and
        // examine the results of - code that interfaces with the core
        return CoreTestHarness(std::move(core), storage, *client_addr);
    }
};
