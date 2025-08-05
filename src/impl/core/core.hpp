#pragma once

#include <cinttypes>
#include <vector>
#include <memory>
#include <thread>
#include <optional>

#include "core/types.hpp"
#include "core/block.hpp"
#include "core/feature.hpp"
#include "core/context.hpp"
#include "core/queue.hpp"
#include "core/storage.hpp"

namespace datadog::platform { class IStorageDirectory; }
namespace datadog::platform { class IHttpSubsystem; }
namespace datadog::platform { class IHttpClient; }

namespace datadog::impl {

enum class CoreState : uint8_t
{
    Uninitialized,
    Initialized,
    Started,
};

struct RegisteredFeature
{
    FeatureId id;
    std::string name;
    std::shared_ptr<Feature> impl;
    std::unique_ptr<platform::IDirectory> directory;
    std::unique_ptr<EventStorage> event_storage;

    RegisteredFeature(
        FeatureId in_id,
        std::string_view in_name,
        std::shared_ptr<Feature> in_impl,
        std::unique_ptr<platform::IDirectory>&& in_directory,
        std::unique_ptr<EventStorage>&& in_event_storage
    )
        : id(in_id)
        , name(in_name)
        , impl(in_impl)
        , directory(std::move(in_directory))
        , event_storage(std::move(in_event_storage))
    {
    }
};

/**
 * Implements the core business logic of the Datadog SDK.
 * 
 * The entry point to the C API is a series of functions that operate on dd_core_t, e.g.
 * dd_core_init(). The entry point to the C++ API is the datadog::Core type.
 * datadog::impl::Core handles API calls from both of those interfaces.
 * 
 * Internally, the core has a few responsibilities:
 * 
 * - It initializes platform-specific subsystems (e.g. filesystem-backed storage, HTTP
 *   client functionality) using the interfaces defined in datadog::platform.
 * 
 * - It interoperates with the different modular features (e.g. logging, RUM, etc.) that
 *   have been registered with it, maintaining a RegisteredFeature object for each. From the
 *   core's perspective, a feature is a child component that:
 * 
 *     1. Produces blocks of feature-specific data to be written to storage
 *     2. Generates reports (i.e. HTTP requests) to send batches
 * 
 * - It maintains a storage thread that flushes
 * 
 * - It maintainsa "core context" containing the metadata
 * 

 * 

 * 
 * - It manages the set of 
 * 
 * - It maintains a "core context" containing the metadata that's relevant for all
 *   features when generating reports.
 * 

 * 
 * - It creates
 * 
 * The core outlives all other 
 */
struct Core
{
    explicit Core(const CoreConfig& config);
    ~Core();

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = default;
    Core& operator=(Core&&) = default;

    void SetTrackingConsent(TrackingConsent value);

    void SetService(std::string_view value);
    void SetEnv(std::string_view value);

    bool Init();

    /**
     * Registers a feature implementation with the core.
     */
    bool RegisterFeature(std::shared_ptr<Feature> impl);

    bool Start();
    void Shutdown();

private:
    bool EnqueueStorageWrite(FeatureId feature_id, Block event, Block event_metadata);

private:
    // Initialized in ctor
    /**
     * Current state of the Core. The API for the Datadog SDK is not thread-safe: all
     * calls must be made from the same thread. Therefore, Core state is checked without
     * synchronization.
     */
    CoreState _state;
    datadog::CoreConfig _config;
    CoreContext _context;

    // Initialized on Init; entirely implementation-controlled
    std::unique_ptr<platform::IStorageDirectory> _storage_root;
    std::unique_ptr<platform::IHttpSubsystem> _http;
    std::unique_ptr<platform::IHttpClient> _http_client;

    // Initialized before Start in response to user-initiated feature registration
    std::vector<RegisteredFeature> _features; // May not be modified after Start()

    // Initialized on Start, cleaned up on Shutdown
    std::unique_ptr<Queue<StorageMessage>> _storage_queue;
    std::optional<std::thread> _storage_thread;
};

}
