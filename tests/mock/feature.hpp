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
 * Most of this state and logic is duplicated; we could feasibly use the actual Core
 * implementation if we make subsystem init non-static and just do some creative
 * static_casting.
 */
struct MockCore
{
    TrackingConsent tracking_consent{ MOCK_CORE_CONFIG.tracking_consent };
    UploadFrequency upload_frequency{ MOCK_CORE_CONFIG.upload_frequency };
    impl::CoreState state{ impl::CoreState::Initialized };
    impl::CoreContext context{ MOCK_CORE_CONFIG };

    // Subsystems created on init; in mock flavors
    std::unique_ptr<MockStorageDirectory> storage_root;
    std::unique_ptr<MockHttpClient> http_client;

    // Registered features
    std::vector<impl::RegisteredFeature> features;

    // Storage and upload thread, tied to Start/Stop
    std::unique_ptr<impl::Queue<impl::StorageMessage>> storage_queue;
    std::optional<std::thread> storage_thread;
    std::unique_ptr<impl::UploadScheduler> upload_scheduler;
    std::optional<std::thread> upload_thread;

    MockCore()
    {
        // Create mock versions of subsystems that the Core uses
        storage_root = std::make_unique<MockStorageDirectory>();
        http_client = std::make_unique<MockHttpClient>();
    }

    template<typename T = MockFeature>
    void RegisterMockFeature(std::shared_ptr<T>& impl)
    {
        // Tests should only register Feature implementations
        static_assert(std::is_base_of<impl::Feature, T>::value, "not a Feature");

        // Tests should not call RegisterMockFeature() after calling Start()
        if (state != impl::CoreState::Initialized)
        {
            assert(false && "RegisterMockFeature called after core start");
            return;
        }

        // Create a subdirectory for this feature in the mock root storage dir
        auto directory_result = storage_root->PrepareSubdirectory(impl->GetName());
        assert(directory_result && "Failed to create mock feature storage directory");
        std::unique_ptr<platform::IDirectory>& feature_subdir = *directory_result;

        // Create directory interfaces for storage thread
        const char* pending_name = impl::EventStorage::PENDING_SUBDIRECTORY_NAME;
        auto pending_subdir = feature_subdir->PrepareSubdirectory(pending_name);
        assert(pending_subdir && "Failed to create mock pending subdir");

        const char* granted_name = impl::EventStorage::GRANTED_SUBDIRECTORY_NAME;
        auto granted_subdir = feature_subdir->PrepareSubdirectory(granted_name);
        assert(granted_subdir && "Failed to create mock granted subdir");

        // Create BatchWriters and EventStorage interface for storage thread
        auto event_storage = std::make_unique<impl::EventStorage>(
            TrackingConsent::Granted,
            std::make_unique<impl::BatchWriter>(std::move(*pending_subdir)),
            std::make_unique<impl::BatchWriter>(std::move(*granted_subdir))
        );

        // Create second (reader) wrapper for granted dir, for upload thread
        auto event_read_directory = feature_subdir->PrepareSubdirectory(granted_name);
        assert(event_read_directory && "Failed to create mock event read directory");

        // Create upload thread state for feature-specific timing etc.
        // TODO: Don't pass UploadFrequency enum; pass actual timing parameters
        auto upload_state = std::make_unique<impl::UploadThreadState>(upload_frequency);

        // Register the feature
        features.emplace_back(
            impl->GetId(),
            impl->GetName(),
            impl,
            std::move(feature_subdir),
            std::move(event_storage),
            std::move(*event_read_directory),
            std::move(upload_state)
        );
    }

    void Start()
    {
        // Tests should only call Start() while not running, after registering features
        if (state != impl::CoreState::Initialized)
        {
            assert(false && "Start called while running");
            return;
        }
        if (features.empty())
        {
            assert(false && "Start called with no features registered");
            return;
        }

        // Spin up storage and upload thread
        storage_queue = std::make_unique<impl::Queue<impl::StorageMessage>>();
        storage_thread = std::thread(
            impl::StorageThreadMain, std::ref(*storage_queue), std::ref(features)
        );
        upload_scheduler = std::make_unique<impl::UploadScheduler>();
        upload_thread = std::thread(
            impl::UploadThreadMain,
            std::ref(context),
            std::ref(*upload_scheduler),
            std::ref(features),
            std::ref(*http_client)
        );

        // Started!
        state = impl::CoreState::Started;

        // Route OnCoreStarted to all features, and install event-gen callback
        for (const auto& feature : features)
        {
            const impl::FeatureId id = feature.id;
            impl::EventGeneratedFunc event_callback =
                [this, id](impl::Block event, impl::Block event_metadata) -> bool
            {
                return EnqueueStorageWrite(id, event, event_metadata);
            };
            feature.impl->OnCoreStarted(event_callback);
        }
    }

    void Stop()
    {
        // Tests should only call Stop() while running
        if (state != impl::CoreState::Started)
        {
            assert(false && "Stop called while not running");
            return;
        }

        // Route OnCoreStopping to all features
        for (const auto& feature : features)
        {
            feature.impl->OnCoreStopping();
        }

        // Shut down storage thread, then upload thread
        storage_queue->Stop();
        storage_thread->join();
        storage_thread.reset();
        storage_queue.reset();
        upload_scheduler->Stop();
        upload_thread->join();
        upload_thread.reset();
        upload_scheduler.reset();

        // Stopped!
        state = impl::CoreState::Initialized;
    }

    bool EnqueueStorageWrite(
        impl::FeatureId feature_id,
        impl::Block event,
        impl::Block event_metadata
    )
    {
        if (state != impl::CoreState::Started)
        {
            return false;
        }
        return storage_queue->Push(
            impl::StorageMessage::EventGenerated(feature_id, event, event_metadata)
        );
    }
};
