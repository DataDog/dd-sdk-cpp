// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>

#include "datadog/core.h"
#include "datadog/core.hpp"
#include "platform/clock.hpp"

namespace datadog {

inline TrackingConsent TrackingConsent_FromC(dd_tracking_consent_t value) {
  static_assert(
      static_cast<int>(TrackingConsent::Granted) == DD_TRACKING_CONSENT_GRANTED
  );
  static_assert(
      static_cast<int>(TrackingConsent::NotGranted) == DD_TRACKING_CONSENT_NOT_GRANTED
  );
  static_assert(
      static_cast<int>(TrackingConsent::Pending) == DD_TRACKING_CONSENT_PENDING
  );
  return static_cast<TrackingConsent>(value);
}

inline const char* TrackingConsent_ToString(TrackingConsent value) {
  switch (value) {
    case TrackingConsent::Granted:
      return "Granted";
    case TrackingConsent::NotGranted:
      return "NotGranted";
    case TrackingConsent::Pending:
      return "Pending";
    default:
      return "";
  }
}

inline Site Site_FromC(dd_site_t value) {
  static_assert(static_cast<int>(Site::us1) == DD_SITE_US1);
  static_assert(static_cast<int>(Site::us3) == DD_SITE_US3);
  static_assert(static_cast<int>(Site::us5) == DD_SITE_US5);
  static_assert(static_cast<int>(Site::eu1) == DD_SITE_EU1);
  static_assert(static_cast<int>(Site::ap1) == DD_SITE_AP1);
  static_assert(static_cast<int>(Site::ap2) == DD_SITE_AP2);
  static_assert(static_cast<int>(Site::us1_fed) == DD_SITE_US1_FED);
  return static_cast<Site>(value);
}

inline const char* Site_ToString(Site value) {
  switch (value) {
    case Site::us1:
      return "us1";
    case Site::us3:
      return "us3";
    case Site::us5:
      return "us5";
    case Site::eu1:
      return "eu1";
    case Site::ap1:
      return "ap1";
    case Site::ap2:
      return "ap2";
    case Site::us1_fed:
      return "us1_fed";
    default:
      return "";
  }
}

inline BatchSize BatchSize_FromC(dd_batch_size_t value) {
  static_assert(static_cast<int>(BatchSize::Small) == DD_BATCH_SIZE_SMALL);
  static_assert(static_cast<int>(BatchSize::Medium) == DD_BATCH_SIZE_MEDIUM);
  static_assert(static_cast<int>(BatchSize::Large) == DD_BATCH_SIZE_LARGE);
  return static_cast<BatchSize>(value);
}

inline const char* BatchSize_ToString(BatchSize value) {
  switch (value) {
    case BatchSize::Small:
      return "Small";
    case BatchSize::Medium:
      return "Medium";
    case BatchSize::Large:
      return "Large";
    default:
      return "";
  }
}

inline platform::Duration BatchSize_ToFileTimingCutoff(BatchSize value) {
  switch (value) {
    case BatchSize::Small:
      return std::chrono::duration_cast<platform::Duration>(std::chrono::seconds(3));
    case BatchSize::Medium:
    default:
      return std::chrono::duration_cast<platform::Duration>(std::chrono::seconds(10));
    case BatchSize::Large:
      return std::chrono::duration_cast<platform::Duration>(std::chrono::seconds(35));
  }
}

inline platform::Duration BatchSize_ToMaxFileAgeForWrite(BatchSize value) {
  const platform::Duration cutoff = BatchSize_ToFileTimingCutoff(value);
  const int64_t five_percent =
      static_cast<int64_t>(static_cast<double>(cutoff.count()) * 0.05);
  return cutoff - platform::Duration{five_percent};
}

inline platform::Duration BatchSize_ToMinFileAgeForRead(BatchSize value) {
  const platform::Duration cutoff = BatchSize_ToFileTimingCutoff(value);
  const int64_t five_percent =
      static_cast<int64_t>(static_cast<double>(cutoff.count()) * 0.05);
  return cutoff + platform::Duration{five_percent};
}

inline UploadFrequency UploadFrequency_FromC(dd_upload_frequency_t value) {
  static_assert(
      static_cast<int>(UploadFrequency::Frequent) == DD_UPLOAD_FREQUENCY_FREQUENT
  );
  static_assert(
      static_cast<int>(UploadFrequency::Average) == DD_UPLOAD_FREQUENCY_AVERAGE
  );
  static_assert(static_cast<int>(UploadFrequency::Rare) == DD_UPLOAD_FREQUENCY_RARE);
  return static_cast<UploadFrequency>(value);
}

inline const char* UploadFrequency_ToString(UploadFrequency value) {
  switch (value) {
    case UploadFrequency::Frequent:
      return "Frequent";
    case UploadFrequency::Average:
      return "Average";
    case UploadFrequency::Rare:
      return "Rare";
    default:
      return "";
  }
}

inline BatchProcessingLevel BatchProcessingLevel_FromC(
    dd_batch_processing_level_t value
) {
  static_assert(
      static_cast<int>(BatchProcessingLevel::Low) == DD_BATCH_PROCESSING_LEVEL_LOW
  );
  static_assert(
      static_cast<int>(BatchProcessingLevel::Medium) == DD_BATCH_PROCESSING_LEVEL_MEDIUM
  );
  static_assert(
      static_cast<int>(BatchProcessingLevel::High) == DD_BATCH_PROCESSING_LEVEL_HIGH
  );
  return static_cast<BatchProcessingLevel>(value);
}

inline size_t BatchProcessingLevel_ToMaxBatchesPerCycle(BatchProcessingLevel value) {
  switch (value) {
    case BatchProcessingLevel::Low:
      return 1;
    case BatchProcessingLevel::Medium:
    default:
      return 20;
    case BatchProcessingLevel::High:
      return 100;
  }
}

inline const char* BatchProcessingLevel_ToString(BatchProcessingLevel value) {
  switch (value) {
    case BatchProcessingLevel::Low:
      return "Low";
    case BatchProcessingLevel::Medium:
      return "Medium";
    case BatchProcessingLevel::High:
      return "High";
    default:
      return "";
  }
}

inline CoreConfig CoreConfig_FromC(const dd_core_config_t& config) {
  // Convert all of the C struct's string values to std::string_view safely
  std::string_view client_token =
      config.client_token != nullptr ? config.client_token : "";
  std::string_view service = config.service != nullptr ? config.service : "";
  std::string_view env = config.env != nullptr ? config.env : "";
  std::string_view application_version =
      config.application_version ? config.application_version : "";

  // Initialize a C++ config struct from our input values
  auto cpp_config =
      CoreConfig(client_token, service, env)
          .SetInitialTrackingConsent(TrackingConsent_FromC(config.tracking_consent))
          .SetSite(Site_FromC(config.site))
          .SetApplicationVersion(application_version)
          .SetBatchSize(BatchSize_FromC(config.batch_size))
          .SetUploadFrequency(UploadFrequency_FromC(config.upload_frequency))
          .SetBatchProcessingLevel(
              BatchProcessingLevel_FromC(config.batch_processing_level)
          );

  // Handle internal options
  if (config.internal_options.flush_http_requests_on_stop) {
    cpp_config.Internal_FlushHttpRequestsOnStop();
  }
  if (config.internal_options.custom_endpoint_url &&
      config.internal_options.custom_endpoint_url[0]) {
    cpp_config.Internal_UseCustomEndpoint(config.internal_options.custom_endpoint_url);
  }

  return cpp_config;
}

}  // namespace datadog
