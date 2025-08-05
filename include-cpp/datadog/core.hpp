#pragma once

#include <string>
#include <memory>

namespace datadog {

// Forward declarations
namespace impl { struct Core; }

enum class TrackingConsent {
    Granted,
    NotGranted,
    Pending,
};

enum class Site {
    us1,
    us3,
    us5,
    eu1,
    ap1,
    ap2,
    us1_fed,
};

/**
 * Controls the timing of reads and writes involving files that store batches of event
 * data prior to upload, indirectly affecting both the size of those files and the
 * immediacy with which they are uploaded, and in turn affecting the size and frequency
 * of network requests.
 * 
 * For example, assuming we apply a cooldown period of +/-5% in order to stagger writes
 * and reads, with these example values:
 * 
 * | BatchSize value   | MAX_AGE_FOR_WRITE | MIN_AGE_FOR_READ |
 * |-------------------|-------------------|------------------|
 * | Small   (3,000ms) |  2.85 seconds     |  3.15 seconds    |
 * | Medium (10,000ms) |  9.50 seconds     | 10.50 seconds    |
 * | Large  (35,000ms) | 33.25 seconds     | 36.75 seconds    |
 * 
 * (Actual behavior is implementation-defined; the API does not guarantee the accuracy
 *  of these example values.)
 * 
 * This means:
 * - we won't write to a file once it's older than MAX_AGE_FOR_WRITE
 * - we won't read from a file until it's MIN_AGE_FOR_READ
 * 
 * At lower durations, files will be smaller, and the SDK will make smaller but more
 * frequent network requests. At higher durations, files will be larger, the SDK will
 * make larger but less frequent network requests, and the worst-case lead time between
 * an event being recorded and the resulting data being sent to Datadog will be higher.
 * 
 * Note that this value only controls the *timing* of file write/read behavior; it does
 * impose a direct, hard limit on the size of files or requests. The SDK imposes such
 * limits internally.
 */
enum class BatchSize {
    Small,
    Medium,
    Large,
};

/**
 * Controls how often the upload thread attempts to process and upload data from batches
 * that are ready for read. A higher frequency means a shorter delay between upload
 * attempts, leading to more
 * 
 * - Adaptive backoff
 */
enum class UploadFrequency {
    Frequent,
    Average,
    Rare,
};

/**
 * Controls how
 */
enum class BatchProcessingLevel {
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
