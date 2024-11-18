// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <memory>
#include <string_view>

#include "datadog/internal/utils.h"
// TODO(RUM-7075): Provide a way to remove libcurl
#include "datadog/reporting/libcurl_reporter.h"
#include "datadog/reporting/reporter.h"
#include "datadog/storage/datadog_file_system.h"
#include "datadog/time_provider.h"

namespace datadog::core {

using namespace std::string_view_literals;
using internal::no_default;

// Possible values for the Data Tracking Consent given by the user of the app.
//
// This value should be used to grant the permission for Datadog SDK to store
// data collected in Logging, Tracing or RUM and upload it to Datadog servers.
enum class TrackingConsent {
  // The permission to persist and send data to the Datadog servers was granted.
  // Any previously stored pending data will be marked as ready for sent.
  Granted,

  // Any previously stored pending data will be deleted and all Logging, RUM and
  // Tracing events will be dropped from now on, without persisting it in any
  // way.
  NotGranted,

  // All Logging, RUM and Tracing events will be persisted in an intermediate
  // location and will be pending there until `TrackingConsent::Granted` or
  // `TrackingConsent::NotGranted` consent value is set. Based on the next
  // consent value, intermediate data will be send to Datadog or deleted.
  Pending,
};

enum class Site {
  // US based servers.
  // Sends data to [app.datadoghq.com](https://app.datadoghq.com/).
  us1,
  // US based servers.
  // Sends data to [app.datadoghq.com](https://us3.datadoghq.com/).
  us3,
  // US based servers.
  // Sends data to [app.datadoghq.com](https://us5.datadoghq.com/).
  us5,
  // Europe based servers.
  // Sends data to [app.datadoghq.eu](https://app.datadoghq.eu/).
  eu1,
  // Asia based servers.
  // Sends data to [ap1.datadoghq.com](https://ap1.datadoghq.com/).
  ap1,
  // US based servers, FedRAMP compatible.
  // Sends data to [app.ddog-gov.com](https://app.ddog-gov.com/).
  us1_fed
};

// Defines the Datadog SDK policy when batching data together before uploading
// it to Datadog servers. Smaller batches mean smaller but more network
// requests, whereas larger batches mean fewer but larger network requests.
enum class BatchSize {
  // Prefer small-sized data batches.
  Small,
  // Prefer medium-sized data batches.
  Medium,
  // Prefer large-sized data batches.
  Large,
};

// Defines the frequency at which Datadog SDK will try to upload data batches.
enum class UploadFrequency {
  // Try to upload batched data frequently.
  Frequent,
  // Try to upload batched data with a medium frequency.
  Average,
  // Try to upload batched data rarely.
  Rare,
};

// Defines the maximum amount of batches processed sequentially without a delay
// within one reading/uploading cycle.
enum class BatchProcessingLevel {
  Low,
  Medium,
  High,
};

//
struct DatadogConfiguration {
  // The current tracking consent for the user.  See `TrackingConsent`
  no_default<TrackingConsent> tracking_consent;

  // The Datadog site to send data to
  no_default<Site> datadog_site;

  // A client token for RUM or logging/APM. You can obtain this token in
  // Datadog.
  no_default<std::string> client_token;

  // The service name associated with data sent to Datadog. See: [Unified
  // Service
  // Tagging](https://docs.datadoghq.com/getting_started/tagging/unified_service_tagging).
  no_default<std::string> service;

  // The environment name sent to Datadog. You can use env to filter events by
  // environment (for example, "staging" or "production").
  no_default<std::string> env;

  // The version of your application. An empty string will not be sent to
  // Datadog.
  std::string application_version{""sv};

  // Defines the Datadog SDK policy for batching data before uploading it to
  // Datadog servers. See `BatchSize`.  Defaults to `BatchSize::Medium`.
  BatchSize batch_size{BatchSize::Medium};

  // The frequency at which the Datadog SDK tries to upload data batches. See
  // 'UploadFrequency`. Defaults to `UploadFrequency::Average`.
  UploadFrequency upload_frequency{UploadFrequency::Average};

  // Defines the maximum number of batches processed sequentially without a
  // delay. See `BatchProcessingLevel`. Defaults to
  // `BatchProcessingLevel::Medium`
  BatchProcessingLevel batch_processing_level{BatchProcessingLevel::Medium};

  // Override the method Datadog uses to access the file system for caching
  // data.  See `DatadogFileSystem`.
  std::shared_ptr<storage::DatadogFileSystem> file_system{
      std::make_shared<storage::StdDatadogFileSystem>()};

  // Override the method for creating a DatadogReporter. See `DatadogReporter`
  // TODO(RUM-7075): Provide a way to remove libcurl
  reporting::DatadogReporter::CreateFunc reporter_create_func{
      reporting::LibcurlReporter::Create};

  // Override the method Datadog uses to access the current time. See
  // `DateTimeProvider`.
  DateTimeProvider date_time_provider{DefaultDateTimeProvider};
};

}  // namespace datadog::core
