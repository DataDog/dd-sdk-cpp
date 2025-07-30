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

enum class BatchSize {
    Small,
    Medium,
    Large,
};

enum class UploadFrequency {
    Frequent,
    Average,
    Rare,
};

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

    void Start();
    void Shutdown();

private:
    std::unique_ptr<impl::Core> _impl;

    friend class Logging;
};

}
