// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string>

#include "datadog/api.hpp"

// Forward declarations
struct CoreTestHarness;

namespace datadog {

// Forward declarations
namespace impl {
class Core;
struct CoreContext;
}  // namespace impl

/**
 * Indicates whether the end user has consented to tracking, as determined by your
 * application.
 *
 * If consent is pending (the default state), the SDK will store data locally but will
 * not upload it until consent is granted. If consent is explicitly revoked (i.e. set to
 * 'not granted'), the SDK will cease storing data locally.
 */
enum class TrackingConsent : uint8_t {
  Granted,
  NotGranted,
  Pending,
};

/**
 * The Datadog datacenter in which your organization's data is stored.
 *
 * See: https://docs.datadoghq.com/getting_started/site/#access-the-datadog-site
 */
enum class Site : uint8_t {
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
enum class BatchSize : uint8_t {
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
enum class UploadFrequency : uint8_t {
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
enum class BatchProcessingLevel : uint8_t {
  Low,
  Medium,
  High,
};

/**
 * Top-level configuration options for the Datadog SDK.
 */
struct CoreConfig {
  friend class Core;
  friend class impl::Core;
  friend struct impl::CoreContext;

 private:
  TrackingConsent tracking_consent{TrackingConsent::Pending};
  Site site{Site::us1};
  std::string client_token;
  std::string service;
  std::string env;
  std::string application_version;
  BatchSize batch_size{BatchSize::Medium};
  UploadFrequency upload_frequency{UploadFrequency::Average};
  BatchProcessingLevel batch_processing_level{BatchProcessingLevel::Medium};

  struct InternalOptions {
    size_t num_http_requests_per_feature_to_flush_on_stop{0};
    std::string custom_endpoint_url;
  } internal_options;

 public:
  /**
   * Initializes a new CoreConfig with the set of values that are required for the SDK
   * to function. Use setter functions to configure optional values.
   *
   * @param in_client_token The client token associated with your application. This
   *  value can be found under RUM Applications (https://app.datadoghq.com/rum/list), in
   *  the "SDK Configuration" settings for your Application, or in your Organization
   *  Settings, under "Client Tokens"
   *  (https://app.datadoghq.com/organization-settings/client-tokens).
   * @param in_service The name of the application, service, or component being
   *  monitored.
   * @param in_env The environment in which this application is running, e.g. 'prod',
   *  'dev', 'staging', 'testing', etc.
   */
  DATADOG_API CoreConfig(
      std::string_view in_client_token, std::string_view in_service,
      std::string_view in_env
  );

  // CoreConfig is copyable and movable
  DATADOG_API ~CoreConfig();
  DATADOG_API CoreConfig(const CoreConfig&);
  DATADOG_API CoreConfig& operator=(const CoreConfig&);
  DATADOG_API CoreConfig(CoreConfig&&);
  DATADOG_API CoreConfig& operator=(CoreConfig&&);

  /**
   * Sets the tracking consent value used on SDK startup. Defaults to Pending.
   *
   * If the user's tracking consent changes after the SDK is initialized, call
   * @ref datadog::Core::SetTrackingConsent() to update it at runtime.
   */
  DATADOG_API CoreConfig& SetInitialTrackingConsent(TrackingConsent value);

  /**
   * Sets the site (i.e. Datadog datacenter) where data for your organization is stored.
   * Defaults to us1.
   */
  DATADOG_API CoreConfig& SetSite(Site value);

  /**
   * Sets the client token value, overriding the value passed in the constructor.
   */
  DATADOG_API CoreConfig& SetClientToken(std::string_view value);

  /**
   * Sets the 'service' value, overriding the value passed in the constructor.
   */
  DATADOG_API CoreConfig& SetService(std::string_view value);

  /**
   * Sets the 'env' value, overriding the value passed in the constructor.
   */
  DATADOG_API CoreConfig& SetEnv(std::string_view value);

  /**
   * Set the 'version' value, identifying the version of your applicating that's being
   * monitored.
   */
  DATADOG_API CoreConfig& SetApplicationVersion(std::string_view value);

  /**
   * Configures the SDK's batch size, which informs how quickly it will consider a batch
   * of event data ready for upload.
   *
   * @see @ref datadog::BatchSize
   */
  DATADOG_API CoreConfig& SetBatchSize(BatchSize value);

  /**
   * Configures the SDK's upload frequency, which informs how frequently it will check
   * for new batches of events to upload.
   *
   * @see @ref datadog::UploadFrequency
   */
  DATADOG_API CoreConfig& SetUploadFrequency(UploadFrequency value);

  /**
   * Configures the SDK's batch processing level, which limits the number of batches
   * that will be uploaded in a given upload cycle.
   *
   * @see @ref datadog::BatchProcessingLevel
   */
  DATADOG_API CoreConfig& SetBatchProcessingLevel(BatchProcessingLevel value);

  /**
   * FOR INTERNAL USE ONLY - This function is not part of the public API and may change
   * without notice. Do not use in production code.
   */
  DATADOG_API CoreConfig& Internal_FlushHttpRequestsOnStop();

  /**
   * FOR INTERNAL USE ONLY - This function is not part of the public API and may change
   * without notice. Do not use in production code.
   */
  DATADOG_API CoreConfig& Internal_UseCustomEndpoint(std::string_view value);
};

/**
 * Top-level interface to the Datadog SDK.
 *
 * Call Core::Create() to create a new core, register your desired set of features on
 * that core (e.g. Logging::Register(core)), and then call Start() to begin the SDK's
 * background processing.
 */
class Core {
 private:
  struct PrivateCtorTag {};

 public:
  // Callers should use Core::Create
  explicit Core(std::unique_ptr<impl::Core>&& impl, PrivateCtorTag);
  DATADOG_API ~Core();

  DATADOG_API static std::shared_ptr<Core> Create(const CoreConfig& config);

  DATADOG_API void SetTrackingConsent(TrackingConsent value);

  DATADOG_API bool Start();
  DATADOG_API void Stop();

 private:
  // Forbid copying/moving: we use std::shared_ptr<Core> at the API boundary
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;
  Core(Core&&) = delete;
  Core& operator=(Core&&) = delete;

  std::unique_ptr<impl::Core> _impl;

  friend class Logging;
  friend class Rum;
  friend struct ::CoreTestHarness;
};

}  // namespace datadog
