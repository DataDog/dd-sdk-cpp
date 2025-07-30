#pragma once

#include "datadog/core.h"
#include "datadog/core.hpp"

namespace datadog {

static TrackingConsent TrackingConsent_FromC(dd_tracking_consent_t value)
{
    static_assert(static_cast<int>(TrackingConsent::Granted) == DD_TRACKING_CONSENT_GRANTED);
    static_assert(static_cast<int>(TrackingConsent::NotGranted) == DD_TRACKING_CONSENT_NOT_GRANTED);
    static_assert(static_cast<int>(TrackingConsent::Pending) == DD_TRACKING_CONSENT_PENDING);
    return static_cast<TrackingConsent>(value);
}

static const char* TrackingConsent_ToString(TrackingConsent value)
{
    switch (value)
    {
        case TrackingConsent::Granted: return "Granted";
        case TrackingConsent::NotGranted: return "NotGranted";
        case TrackingConsent::Pending: return "Pending";
        default: return "";
    }
}

static Site Site_FromC(dd_site_t value)
{
    static_assert(static_cast<int>(Site::us1) == DD_SITE_US1);
    static_assert(static_cast<int>(Site::us3) == DD_SITE_US3);
    static_assert(static_cast<int>(Site::us5) == DD_SITE_US5);
    static_assert(static_cast<int>(Site::eu1) == DD_SITE_EU1);
    static_assert(static_cast<int>(Site::ap1) == DD_SITE_AP1);
    static_assert(static_cast<int>(Site::ap2) == DD_SITE_AP2);
    static_assert(static_cast<int>(Site::us1_fed) == DD_SITE_US1_FED);
    return static_cast<Site>(value);
}

static const char* Site_ToString(Site value)
{
    switch (value)
    {
        case Site::us1: return "us1";
        case Site::us3: return "us3";
        case Site::us5: return "us5";
        case Site::eu1: return "eu1";
        case Site::ap1: return "ap1";
        case Site::ap2: return "ap2";
        case Site::us1_fed: return "us1_fed";
        default: return "";
    }
}

static BatchSize BatchSize_FromC(dd_batch_size_t value)
{
    static_assert(static_cast<int>(BatchSize::Small) == DD_BATCH_SIZE_SMALL);
    static_assert(static_cast<int>(BatchSize::Medium) == DD_BATCH_SIZE_MEDIUM);
    static_assert(static_cast<int>(BatchSize::Large) == DD_BATCH_SIZE_LARGE);
    return static_cast<BatchSize>(value);
}

static const char* BatchSize_ToString(BatchSize value)
{
    switch (value)
    {
        case BatchSize::Small: return "Small";
        case BatchSize::Medium: return "Medium";
        case BatchSize::Large: return "Large";
        default: return "";
    }
}

static UploadFrequency UploadFrequency_FromC(dd_upload_frequency_t value)
{
    static_assert(static_cast<int>(UploadFrequency::Frequent) == DD_UPLOAD_FREQUENCY_FREQUENT);
    static_assert(static_cast<int>(UploadFrequency::Average) == DD_UPLOAD_FREQUENCY_AVERAGE);
    static_assert(static_cast<int>(UploadFrequency::Rare) == DD_UPLOAD_FREQUENCY_RARE);
    return static_cast<UploadFrequency>(value);
}

static const char* UploadFrequency_ToString(UploadFrequency value)
{
    switch (value)
    {
        case UploadFrequency::Frequent: return "Frequent";
        case UploadFrequency::Average: return "Average";
        case UploadFrequency::Rare: return "Rare";
        default: return "";
    }
}

static BatchProcessingLevel BatchProcessingLevel_FromC(dd_batch_processing_level_t value)
{
    static_assert(static_cast<int>(BatchProcessingLevel::Low) == DD_BATCH_PROCESSING_LEVEL_LOW);
    static_assert(static_cast<int>(BatchProcessingLevel::Medium) == DD_BATCH_PROCESSING_LEVEL_MEDIUM);
    static_assert(static_cast<int>(BatchProcessingLevel::High) == DD_BATCH_PROCESSING_LEVEL_HIGH);
    return static_cast<BatchProcessingLevel>(value);
}

static const char* BatchProcessingLevel_ToString(BatchProcessingLevel value)
{
    switch (value)
    {
        case BatchProcessingLevel::Low: return "Low";
        case BatchProcessingLevel::Medium: return "Medium";
        case BatchProcessingLevel::High: return "High";
        default: return "";
    }
}

static CoreConfig CoreConfig_FromC(const dd_core_config_t& config) {
    return CoreConfig{
        .tracking_consent = TrackingConsent_FromC(config.tracking_consent),
        .datadog_site = Site_FromC(config.datadog_site),
        .client_token = config.client_token ? std::string(config.client_token) : "",
        .service = config.service ? std::string(config.service) : "",
        .env = config.env ? std::string(config.env) : "",
        .application_version = config.application_version ? std::string(config.env) : "",
        .batch_size = BatchSize_FromC(config.batch_size),
        .upload_frequency = UploadFrequency_FromC(config.upload_frequency),
        .batch_processing_level = BatchProcessingLevel_FromC(config.batch_processing_level),
    };
}

}
