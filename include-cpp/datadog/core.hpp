#pragma once

#include <memory>
#include <string>

namespace datadog {

// Forward declarations
namespace impl {
struct Core;
}

enum class TrackingConsent
{
    Granted,
    NotGranted,
    Pending,
};

enum class Site
{
    us1,
    us3,
    us5,
    eu1,
    ap1,
    ap2,
    us1_fed,
};

/**
 * Determines how long the SDK will accumulate events in a single batch before releasing
 * that batch to be processed in the next upload cycle.
 *
 * As events are produced by individual features, a storage thread flushes them to
 * persistent storage, batching multiple events together in the same file. Separately,
 * an upload thread periodically runs upload cycles, which check for files that are
 * ready to be processed and uploaded for a given feature.
 *
 * Storage and upload threads are synchronized using a time-based mechanism: the storage
 * thread will not write to a file past a certain age, and the upload thread will not
 * read from a file until it exceeds a certain age. This value controls that timing
 * threshold.
 *
 * With a value of 'small', the storage thread will stop writing to batches sooner,
 * allowing the upload thread to process them more expediently. As a result, the SDK
 * will make smaller but more frequent HTTP requests. With a value of 'large', the SDK
 * will make larger but less frequent HTTP requests, and the worst-case lead time
 * between an event being recorded and the resulting data being sent to Datadog will be
 * higher.
 *
 * This value only controls the timing of storage and upload threads; it does not impose
 * a direct, hard limit on the size of files or requests. The SDK imposes such limits
 * internally.
 */
enum class BatchSize
{
    Small,
    Medium,
    Large,
};

/**
 * Determines how often upload cycles occur for any given feature.
 *
 * Upload cycles are scheduled periodically, becoming more frequent when network
 * conditions are good and HTTP requests are reliably succeeding for the associated
 * feature, and becoming less frequent in response to adverse conditions.
 *
 * With a value of 'frequent', the best-case and worst-case delay between upload cycles
 * will be shorter, reducing the lead time between storage and upload for any given
 * batch of events. With a value of 'rare', the SDK will make HTTP requests less
 * frequently, but each burst of requests will tend to be larger.
 *
 * This value only controls the frequency with which upload cycles are initiated; it
 * does not restrict the timing of uploads within a single cycle. If multiple batches
 * are available for a given feature when an upload cycle runs, the upload thread will
 * process those batches sequentially, without delay, up to a limit determined by
 * BatchProcessingLevel.
 */
enum class UploadFrequency
{
    Frequent,
    Average,
    Rare,
};

/**
 * Determines the maximum number of batches that may be processed and uploaded for a
 * given feature within a single upload cycle.
 *
 * Lower values reduce HTTP request burstiness at the cost of throughput. Higher values
 * maximize throughput by processing more batches per cycle, potentially creating bursts
 * of HTTP requests.
 */
enum class BatchProcessingLevel
{
    Low,
    Medium,
    High,
};

struct CoreConfig
{
    TrackingConsent tracking_consent;
    Site datadog_site;
    std::string client_token;
    std::string service;
    std::string env;
    std::string application_version;
    BatchSize batch_size;
    UploadFrequency upload_frequency;
    BatchProcessingLevel batch_processing_level;
};

struct Core
{
    static std::shared_ptr<Core> Create(const CoreConfig& config);

    bool Start();
    void Shutdown();

  private:
    std::unique_ptr<impl::Core> _impl;

    friend class Logging;
};

}
